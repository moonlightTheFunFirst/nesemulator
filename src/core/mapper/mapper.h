#ifndef NESEMU_MAPPER_H
#define NESEMU_MAPPER_H

#include "nesemu.h"

#include <stdint.h>

const NesMapperOps *nes_mapper_ops_for_id(uint8_t mapper_id);

#endif
