/* Separate60Hz ship solver; legacy gameplay clocks keep their original cadence. */
BOOL wo_sim_initialize(BYTE *image);
void wo_sim_reset(void);
BOOL wo_sim_half_tick(void);
BOOL wo_sim_wants_half_tick(void);
void wo_sim_latch_input(void);
void wo_sim_restore_input(void);
DWORD wo_sim_extra_serial(void);
void wo_sim_trail_shift(int ship,float *out);
