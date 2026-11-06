/* Background jobs for Setup. Each job runs on its own thread and reports
 * through a thread-safe event queue that the UI drains every frame. */
#ifndef SETUP_WORK_H
#define SETUP_WORK_H

#include "core/wo_mod.h"
#include "core/wo_ops.h"
#include "core/wo_update.h"

typedef enum JobKind {
    JOB_DISCOVER, /* search common folders for game sources */
    JOB_PROBE,    /* verify the two source fields */
    JOB_CHECK,    /* reconcile the installation (repair only if needed) */
    JOB_INSTALL,  /* first installation */
    JOB_REPAIR,   /* forced Install / repair */
    JOB_UPDATE    /* download and install the latest release */
} JobKind;

typedef struct JobRequest {
    JobKind kind;
    WoConfig config;    /* JOB_INSTALL */
    char *probe_game;   /* JOB_PROBE */
    char *probe_psx;
    char *exe_path;     /* JOB_UPDATE: the running Setup.exe */
} JobRequest;

typedef struct JobEvent {
    struct JobEvent *next;
    JobKind kind;
    bool done;          /* false: progress update */
    bool success;
    int percent;
    char *message;      /* progress text, or the final (user-facing) message */
    WoDiscovery discovery;
    WoSourceInfo game_info;
    WoSourceInfo psx_info;
    WoUpdateResult update;
} JobEvent;

/* mod must outlive every job. wake is called (from any thread) after an
 * event is queued so a waiting UI loop can wake up. */
void jobs_init(const WoMod *mod, void (*wake)(void));
/* Start a job; takes ownership of the request's strings and config. */
bool job_start(JobRequest *request, volatile long *cancel);
JobEvent *job_poll(void);
void job_event_free(JobEvent *event);
/* Number of jobs of this kind still running. */
long jobs_running(JobKind kind);
long jobs_running_total(void);

#endif
