#include "mapper.h"
#include "mapper0.h"
#include "mapper3.h"
#include "mapper4.h"
#include "mapper10.h"
#include "mapper19.h"
#include "mapper206.h"
#include "mapper88.h"

#include <stddef.h>

const NesMapperOps *nes_mapper_ops_for_id(uint8_t mapper_id)
{
    static const NesMapperOps *const ops[] = {
        &nes_mapper0_ops,
        &nes_mapper3_ops,
        &nes_mapper4_ops,
        &nes_mapper10_ops,
        &nes_mapper19_ops,
        &nes_mapper206_ops,
        &nes_mapper88_ops
    };
    size_t i;

    for (i = 0; i < sizeof(ops) / sizeof(ops[0]); ++i) {
        if (ops[i]->mapper_id == mapper_id) {
            return ops[i];
        }
    }
    return NULL;
}
