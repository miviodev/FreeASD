#include "common.h"

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    // В данной ОС pwd обычно реализуется шеллом, но мы можем попробовать 
    // получить CWD если ядро это поддерживает. Если нет - просто заглушка.
    // Для ASD сейчас CWD хранится в шелле.
    puts("CWD is managed by shell in this version of ASD");
    return 0;
}
