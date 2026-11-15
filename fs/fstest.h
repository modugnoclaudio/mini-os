/*
 * fs/fstest.h — Prove automatiche del filesystem in RAM.
 * Il codice è in fs/fstest.c. Va chiamata dopo fs_init().
 */
#ifndef FS_FSTEST_H
#define FS_FSTEST_H

/* Esegue le prove e stampa il riepilogo ("File system: tutte le prove superate"). */
void fstest_run(void);

#endif /* FS_FSTEST_H */
