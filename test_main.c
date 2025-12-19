#include <glib.h>
#include <gtk/gtk.h>

// This is not ideal, but for a single file program, it's the easiest way to get access to static functions.
#define main app_main 
#include "main.c"
#undef main


static void test_writememorycallback() {
    struct MemoryStruct mem;
    mem.memory = malloc(1);
    mem.size = 0;

    char *test_string = "Hello, world!";
    size_t test_string_len = strlen(test_string);

    size_t returned_size = WriteMemoryCallback(test_string, 1, test_string_len, &mem);

    g_assert_cmpint(returned_size, ==, test_string_len);
    g_assert_cmpint(mem.size, ==, test_string_len);
    g_assert_cmpstr(mem.memory, ==, test_string);

    char *test_string2 = " Another string.";
    size_t test_string2_len = strlen(test_string2);

    returned_size = WriteMemoryCallback(test_string2, 1, test_string2_len, &mem);

    g_assert_cmpint(returned_size, ==, test_string2_len);
    g_assert_cmpint(mem.size, ==, test_string_len + test_string2_len);
    g_assert_cmpstr(mem.memory, ==, "Hello, world! Another string.");

    free(mem.memory);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/writememorycallback/basic", test_writememorycallback);
    return g_test_run();
}
