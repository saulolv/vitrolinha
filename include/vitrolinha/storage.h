#ifndef VITROLINHA_STORAGE_H_
#define VITROLINHA_STORAGE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <vitrolinha/track.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STORAGE_FILE_MAX 2032U

int storage_init(void);
int lsdir(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* VITROLINHA_STORAGE_H_ */
