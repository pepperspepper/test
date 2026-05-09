//
// Created by Perfare on 2020/7/4.
//

#ifndef ZYGISK_IL2CPPDUMPER_IL2CPP_DUMP_H
#define ZYGISK_IL2CPPDUMPER_IL2CPP_DUMP_H

void il2cpp_api_init(void *handle);


#ifdef __cplusplus
extern "C" {
#endif
void set_il2cpp_base(void* addr);
#ifdef __cplusplus
}
#endif

void il2cpp_dump(const char *outDir);

#endif //ZYGISK_IL2CPPDUMPER_IL2CPP_DUMP_H
