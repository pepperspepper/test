//
// Created by Perfare on 2020/7/4.
//

#ifndef ZYGISK_IL2CPPDUMPER_HACK_H
#define ZYGISK_IL2CPPDUMPER_HACK_H

#include <stddef.h>

void hack_prepare(const char *game_data_dir, void *data, int target_pid, size_t length);

#endif //ZYGISK_IL2CPPDUMPER_HACK_H
