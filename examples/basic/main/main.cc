#include "my_linklist.h"
#include "esp_log.h"

extern "C" void app_main()
{
    MyList<int, 8> list;
    list.push_back(112);
    int out;
    list.pop_front(out);
    ESP_LOGI("demo", "got %d", out);
}