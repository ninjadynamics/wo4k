#ifndef WO_AUDIO_API_H
#define WO_AUDIO_API_H
UINT wo_audio_bank(void);
UINT wo_audio_tracks(void);
BOOL wo_audio_switch(UINT bank);
UINT wo_audio_pref(const WCHAR *key,UINT fallback);
void wo_audio_save(UINT music,UINT effects,UINT track);
void wo_audio_flush(BOOL force);
#endif
