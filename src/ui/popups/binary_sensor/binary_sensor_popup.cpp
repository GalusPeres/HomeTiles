#include "src/ui/popups/binary_sensor/binary_sensor_popup.h"

#include "src/ui/popups/sensor/sensor_popup.h"

void show_binary_sensor_popup(const BinarySensorPopupInit& init) {
  SensorPopupInit sensor_init;
  sensor_init.entity_id = init.entity_id;
  sensor_init.title = init.title;
  sensor_init.icon_name = init.icon_name;
  sensor_init.value = init.state;
  sensor_init.bg_color = init.bg_color;
  sensor_init.binary_mode = true;
  sensor_init.binary_device_class = init.device_class;
  sensor_init.binary_last_changed = init.last_changed;
  sensor_init.binary_available = init.available;
  sensor_init.binary_icon_override = init.icon_override;
  sensor_init.icon_colors = init.icon_colors;
  show_sensor_popup(sensor_init);
}

void queue_binary_sensor_popup_state(const String& entity_id,
                                     const String& state,
                                     bool available,
                                     const String& device_class,
                                     uint64_t last_changed,
                                     const String& icon_name) {
  queue_sensor_popup_binary_state(entity_id, state, available, device_class,
                                  last_changed, icon_name);
}
