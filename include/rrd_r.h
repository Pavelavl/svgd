/**
 * @file rrd_r.h
 * @brief RRD reading and SVG generation umbrella header
 *
 * This header includes all RRD-related submodules:
 * - reader.h: RRD file reading
 * - cache.h: Data caching
 * - svg.h: SVG generation
 */

#ifndef RRD_READER_H
#define RRD_READER_H

/* Include all submodules */
#include "rrd/reader.h"
#include "rrd/cache.h"
#include "rrd/svg.h"

/* Legacy definitions for backward compatibility */
#define MAX_POINTS 1000

#endif /* RRD_READER_H */
