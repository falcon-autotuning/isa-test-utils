#pragma once
#include "isa-test-utils/isa-test-utils-export.h"
#include "isa-test-utils.h"

ISA_TEST_UTILS_EXPORT char *map_to_json(const Map *map);

// contents are the contents of the file and it returns the path to the file
ISA_TEST_UTILS_EXPORT char *write_script_to_temp(const char *contents);
