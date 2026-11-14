/* vita_trace.c -- DIAGNOSTICO TEMPORAL: marca en que funcion de audio esta el hilo principal.
   Se activa con -Wl,--wrap (ver vita.cmake). vita_log.c imprime vita_stage cada 2 s. */
const char *volatile vita_stage = "";

#define ENTER(n)  (vita_stage = n)
#define LEAVE(n)  (vita_stage = "tras:" n)

void *__real_Mix_LoadMUS(const char *);
void *__wrap_Mix_LoadMUS(const char *f) { ENTER("Mix_LoadMUS"); void *r = __real_Mix_LoadMUS(f); LEAVE("Mix_LoadMUS"); return r; }
void __real_Mix_FreeMusic(void *);
void __wrap_Mix_FreeMusic(void *m) { ENTER("Mix_FreeMusic"); __real_Mix_FreeMusic(m); LEAVE("Mix_FreeMusic"); }
int __real_Mix_PlayMusic(void *, int);
int __wrap_Mix_PlayMusic(void *m, int l) { ENTER("Mix_PlayMusic"); int r = __real_Mix_PlayMusic(m, l); LEAVE("Mix_PlayMusic"); return r; }
int __real_Mix_FadeInMusic(void *, int, int);
int __wrap_Mix_FadeInMusic(void *m, int l, int ms) { ENTER("Mix_FadeInMusic"); int r = __real_Mix_FadeInMusic(m, l, ms); LEAVE("Mix_FadeInMusic"); return r; }
int __real_Mix_HaltMusic(void);
int __wrap_Mix_HaltMusic(void) { ENTER("Mix_HaltMusic"); int r = __real_Mix_HaltMusic(); LEAVE("Mix_HaltMusic"); return r; }
int __real_Mix_FadeOutMusic(int);
int __wrap_Mix_FadeOutMusic(int ms) { ENTER("Mix_FadeOutMusic"); int r = __real_Mix_FadeOutMusic(ms); LEAVE("Mix_FadeOutMusic"); return r; }
void __real_SDL_LockAudio(void);
void __wrap_SDL_LockAudio(void) { ENTER("SDL_LockAudio"); __real_SDL_LockAudio(); LEAVE("SDL_LockAudio"); }
void __real_SDL_LockAudioDevice(unsigned);
void __wrap_SDL_LockAudioDevice(unsigned d) { ENTER("SDL_LockAudioDevice"); __real_SDL_LockAudioDevice(d); LEAVE("SDL_LockAudioDevice"); }
void __real_SDL_CloseAudioDevice(unsigned);
void __wrap_SDL_CloseAudioDevice(unsigned d) { ENTER("SDL_CloseAudioDevice"); __real_SDL_CloseAudioDevice(d); LEAVE("SDL_CloseAudioDevice"); }
void __real_SDL_PauseAudioDevice(unsigned, int);
void __wrap_SDL_PauseAudioDevice(unsigned d, int p) { ENTER("SDL_PauseAudioDevice"); __real_SDL_PauseAudioDevice(d, p); LEAVE("SDL_PauseAudioDevice"); }
void __real_SDL_WaitThread(void *, int *);
void __wrap_SDL_WaitThread(void *t, int *s) { ENTER("SDL_WaitThread"); __real_SDL_WaitThread(t, s); LEAVE("SDL_WaitThread"); }

/* --- vitaGL: gpu_alloc_mapped_temp (memoria temporal de GPU para arrays de vertices) --- */
volatile unsigned vita_alloc_in, vita_alloc_kb, vita_alloc_max_kb, vita_alloc_n;
/* (envoltorio retirado: gpu_alloc_mapped_temp no es un simbolo enlazable, es macro o inline) */
