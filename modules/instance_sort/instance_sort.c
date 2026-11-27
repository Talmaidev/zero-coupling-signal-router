/* Module registration TU for the instance_sort pipeline stage (裁决 7).
 *
 * The stage itself is header-inline (include/zcsr/instance_sort.h) so any
 * renderer TU can compile it without a separate static-lib link edge. This
 * translation unit exists so modules/instance_sort is a registered module
 * under the zero-coupling build rule; it keeps no external symbol.
 */

#include "zcsr/instance_sort.h"

static const char zcsr_instance_sort_module_marker[] = "instance_sort";
