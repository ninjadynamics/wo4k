#include "setup/work.h"

#include "core/wo_win.h"

#include <string.h>

#include "core/wo_log.h"

typedef struct Job {
    JobRequest request;
    volatile long *cancel;
} Job;

static const WoMod *job_mod;
static void (*job_wake)(void);
static SRWLOCK queue_lock = SRWLOCK_INIT;
static JobEvent *queue_head;
static JobEvent *queue_tail;
static volatile long running[JOB_UPDATE + 1];

/* Counter for a job kind; the index is bounds-checked for safety. */
static volatile long *counter(JobKind kind) {
    unsigned index = (unsigned)kind;
    return &running[index <= (unsigned)JOB_UPDATE ? index : (unsigned)JOB_UPDATE];
}

void jobs_init(const WoMod *mod, void (*wake)(void)) {
    job_mod = mod;
    job_wake = wake;
}

static void post(JobEvent *event) {
    AcquireSRWLockExclusive(&queue_lock);
    if (queue_tail) {
        queue_tail->next = event;
    } else {
        queue_head = event;
    }
    queue_tail = event;
    ReleaseSRWLockExclusive(&queue_lock);
    if (job_wake) {
        job_wake();
    }
}

JobEvent *job_poll(void) {
    AcquireSRWLockExclusive(&queue_lock);
    JobEvent *event = queue_head;
    if (event) {
        queue_head = event->next;
        if (!queue_head) {
            queue_tail = NULL;
        }
        event->next = NULL;
    }
    ReleaseSRWLockExclusive(&queue_lock);
    return event;
}

void job_event_free(JobEvent *event) {
    if (!event) {
        return;
    }
    wo_free(event->message);
    wo_discovery_free(&event->discovery);
    wo_source_info_free(&event->game_info);
    wo_source_info_free(&event->psx_info);
    wo_free(event);
}

long jobs_running(JobKind kind) {
    return InterlockedCompareExchange(counter(kind), 0, 0);
}

long jobs_running_total(void) {
    long total = 0;
    for (int kind = 0; kind <= JOB_UPDATE; kind++) {
        total += jobs_running((JobKind)kind);
    }
    return total;
}

typedef struct ProgressContext {
    JobKind kind;
} ProgressContext;

static void report_progress(void *context, int percent, const char *message) {
    ProgressContext *progress = context;
    JobEvent *event = wo_alloc(sizeof(*event), NULL);
    if (!event) {
        return;
    }
    event->kind = progress->kind;
    event->percent = percent;
    event->message = wo_strdup(message ? message : "", NULL);
    post(event);
}

static const char *stage_name(JobKind kind) {
    switch (kind) {
    case JOB_INSTALL:
        return "Installation failed";
    case JOB_REPAIR:
        return "Install / repair failed";
    case JOB_UPDATE:
        return "Update failed";
    case JOB_CHECK:
        return "Checking the configured installation failed";
    default:
        return "Setup failed";
    }
}

static void finish_failure(JobEvent *event, WoStage stage, const WoError *error, JobKind kind) {
    wo_log_failure(stage_name(kind), error);
    event->success = false;
    event->message = wo_user_message(stage, error);
}

static DWORD WINAPI job_thread(LPVOID parameter) {
    Job *job = parameter;
    JobRequest *request = &job->request;
    JobEvent *event = wo_alloc(sizeof(*event), NULL);
    ProgressContext context = {request->kind};
    WoProgress progress = {report_progress, &context, job->cancel, WO_STAGE_GENERAL};
    WoError error = {0};
    if (event) {
        event->kind = request->kind;
        event->done = true;
        event->success = true;
        switch (request->kind) {
        case JOB_DISCOVER:
            wo_discover_sources(job_mod, 12, job->cancel, &event->discovery);
            break;
        case JOB_PROBE:
            wo_source_probe(job_mod, request->probe_game ? request->probe_game : "",
                            &event->game_info);
            wo_source_probe(job_mod, request->probe_psx ? request->probe_psx : "",
                            &event->psx_info);
            break;
        case JOB_CHECK:
        case JOB_REPAIR: {
            WoReconcileResult result;
            bool force = request->kind == JOB_REPAIR;
            if (wo_reconcile(job_mod, force, &progress, &result, &error)) {
                event->message = wo_strdup(force ? "Install / repair completed. Ready to race."
                                                 : "Installation checked and ready.",
                                           NULL);
            } else {
                finish_failure(event, progress.stage, &error, request->kind);
            }
            break;
        }
        case JOB_INSTALL:
            if (wo_install(job_mod, &request->config, false, &progress, &error)) {
                event->message = wo_strdup("Ready to race", NULL);
            } else {
                finish_failure(event, progress.stage, &error, request->kind);
            }
            break;
        case JOB_UPDATE: {
            WoUpdateCallbacks callbacks = {report_progress, &context, job->cancel};
            if (wo_update_run(job_mod, request->exe_path, &callbacks, &event->update, &error)) {
                event->message = wo_strdup(event->update.outcome == WO_UPDATE_INSTALLED
                                               ? "Update installed. Restarting Setup to finish..."
                                               : "WO4K is already up to date.",
                                           NULL);
            } else {
                finish_failure(event, WO_STAGE_UPDATE, &error, request->kind);
            }
            break;
        }
        }
    }
    InterlockedDecrement(counter(request->kind));
    if (event) {
        post(event);
    }
    wo_config_free(&request->config);
    wo_free(request->probe_game);
    wo_free(request->probe_psx);
    wo_free(request->exe_path);
    wo_free(job);
    return 0;
}

bool job_start(JobRequest *request, volatile long *cancel) {
    Job *job = wo_alloc(sizeof(*job), NULL);
    if (!job) {
        return false;
    }
    job->request = *request;
    job->cancel = cancel;
    memset(request, 0, sizeof(*request));
    InterlockedIncrement(counter(job->request.kind));
    HANDLE thread = CreateThread(NULL, 0, job_thread, job, 0, NULL);
    if (!thread) {
        InterlockedDecrement(counter(job->request.kind));
        wo_config_free(&job->request.config);
        wo_free(job->request.probe_game);
        wo_free(job->request.probe_psx);
        wo_free(job->request.exe_path);
        wo_free(job);
        return false;
    }
    CloseHandle(thread);
    return true;
}
