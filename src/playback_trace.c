/**
 * @file playback_trace.c
 * @brief Captura fixa em RAM; exportação no host somente após encerramento.
 *
 * O player serializa os produtores. begin/read/export não concorrem com
 * a reprodução; o caminho do timer não aloca nem acessa arquivos.
 */
#include <vitrolinha/playback_trace.h>

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>

static struct playback_trace_entry entries[PLAYBACK_TRACE_CAPACITY];
static size_t count;
static uint64_t start;
static bool active;
static bool incomplete;

int playback_trace_begin(uint64_t start_us)
{
	if (active) {
		return -EBUSY;
	}
	start = start_us;
	count = 0;
	incomplete = false;
	active = true;
	return 0;
}

int playback_trace_record(uint64_t timestamp_us, uint32_t frequency_hz)
{
	if (!active) {
		return -EACCES;
	}
	if (timestamp_us < start ||
	    (count > 0 && timestamp_us - start < entries[count - 1].elapsed_us)) {
		return -EINVAL;
	}
	if (count == PLAYBACK_TRACE_CAPACITY) {
		incomplete = true;
		return -ENOSPC;
	}
	entries[count++] = (struct playback_trace_entry){
		.elapsed_us = timestamp_us - start, .freq_hz = frequency_hz,
	};
	return 0;
}

int playback_trace_end(uint64_t timestamp_us)
{
	int err = playback_trace_record(timestamp_us, 0);

	if (err == 0 || err == -ENOSPC) {
		active = false;
	}
	return err;
}

int playback_trace_read(size_t index, struct playback_trace_entry *out)
{
	if (out == NULL) {
		return -EINVAL;
	}
	if (active) {
		return -EBUSY;
	}
	if (incomplete) {
		return -ENOSPC;
	}
	if (index >= count) {
		return 0;
	}
	*out = entries[index];
	return 1;
}

int playback_trace_export_csv(const char *path)
{
	if (path == NULL || path[0] == '\0') {
		return -EINVAL;
	}
	if (active) {
		return -EBUSY;
	}
	if (incomplete) {
		return -ENOSPC;
	}
	FILE *file = fopen(path, "w");

	if (file == NULL) {
		return -EIO;
	}
	int err = fprintf(file, "elapsed_us,frequency_hz\n") < 0 ? -EIO : 0;

	for (size_t i = 0; err == 0 && i < count; i++) {
		if (fprintf(file, "%" PRIu64 ",%" PRIu32 "\n",
			    entries[i].elapsed_us, entries[i].freq_hz) < 0) {
			err = -EIO;
		}
	}
	if (fclose(file) != 0) {
		err = -EIO;
	}
	return err;
}
