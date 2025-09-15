#include <libremidi/libremidi-c.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#if defined(_WIN32)
  #include <Windows.h>
#else
  #include <unistd.h>
#endif

void sleep_ms(int milliseconds)
{
#if defined(_WIN32)
  Sleep(milliseconds);
#else
  usleep(milliseconds * 1000);
#endif
}

typedef struct enumerated_ports
{
  libremidi_midi_in_port* in_ports[256];
  libremidi_midi_out_port* out_ports[256];
  int in_port_count;
  int out_port_count;
} enumerated_ports;

static const char* safe_str(const char* s) { return s ? s : "(null)"; }

static void print_identifier(const char* label, const libremidi_identifier* id)
{
  switch (id->value_type) {
  case LIBREMIDI_ID_NONE:
    printf("  %s: (none)\n", label);
    break;

  case LIBREMIDI_ID_UUID:
    printf("  %s (uuid): ", label);
    for (int i = 0; i < 16; ++i)
      printf("%02x", (unsigned)id->value.uuid.bytes[i]);
    printf("\n");
    break;

  case LIBREMIDI_ID_STRING:
    printf("  %s: %s\n", label, safe_str(id->value.string));
    break;

  case LIBREMIDI_ID_UINT64:
    printf("  %s: %" PRIu64 "\n", label, (uint64_t)id->value.u64);
    break;

  default:
    printf("  %s: (unknown type %u)\n", label, (unsigned)id->value_type);
    break;
  }
}

static void print_port_info(const libremidi_port_information* info) {
  print_identifier("container_identification", &info->container_identifier);
  print_identifier("device_identification", &info->device_identifier);

  printf("  client_handle: %" PRId64 "\n", (int64_t)info->client_handle);
  printf("  port_handle:   %" PRId64 "\n", (int64_t)info->port_handle);
  printf("  manufacturer:  %s\n", safe_str(info->manufacturer));
  printf("  device_name:   %s\n", safe_str(info->device_name));
  printf("  port_name:     %s\n", safe_str(info->port_name));
  printf("  display_name:  %s\n", safe_str(info->display_name));

  printf("  port type bits: 0x%02x\n", (unsigned)info->type);
}

void on_input_port_found(void* ctx, const libremidi_midi_in_port* port)
{
  libremidi_port_information info = {0};
  if (libremidi_midi_in_port_information(port, &info) != 0)
    return;

  printf("input: %s\n", safe_str(info.port_name));
  print_port_info(&info);
  fflush(stdout);

  enumerated_ports* e = (enumerated_ports*)ctx;
  libremidi_midi_in_port_clone(port, &e->in_ports[e->in_port_count]);
  e->in_port_count++;
}

void on_output_port_found(void* ctx, const libremidi_midi_out_port* port)
{
  libremidi_port_information info = {0};
  if (libremidi_midi_out_port_information(port, &info) != 0)
    return;

  printf("output: %s\n", safe_str(info.port_name));
  print_port_info(&info);
  fflush(stdout);

  enumerated_ports* e = (enumerated_ports*)ctx;
  libremidi_midi_out_port_clone(port, &e->out_ports[e->out_port_count]);
  e->out_port_count++;
}

void on_midi1_message(
    void* ctx, libremidi_timestamp ts, const libremidi_midi1_symbol* msg, size_t len)
{
  printf("%#02x %#02x %#02x \n", (int)msg[0], (int)msg[1], (int)msg[2]);
  fflush(stdout);
}

void on_midi2_message(
    void* ctx, libremidi_timestamp ts, const libremidi_midi2_symbol* msg, size_t len)
{
  printf("%#02x %#02x %#02x\n", (int)msg[0], (int)msg[1], (int)msg[2]);
  fflush(stdout);
}

int enumerate_ports(libremidi_midi_observer_handle* observer, struct enumerated_ports* e)
{
  int ret = 0;

  ret = libremidi_midi_observer_enumerate_input_ports(observer, e, on_input_port_found);
  if (ret != 0)
    return ret;

  ret = libremidi_midi_observer_enumerate_output_ports(observer, e, on_output_port_found);
  if (ret != 0)
    return ret;

  return 0;
}

int main(void)
{
  int ret = 0;

  /// Create an observer for MIDI ports
  struct enumerated_ports e;
  memset(&e, 0, sizeof(e));

  libremidi_observer_configuration observer_conf;
  ret = libremidi_midi_observer_configuration_init(&observer_conf);
  if (ret != 0)
    return ret;

  observer_conf.track_hardware = true;
  observer_conf.track_virtual = true;
  observer_conf.track_any = true;
  observer_conf.input_added.context = &e;
  observer_conf.input_added.callback = on_input_port_found;
  observer_conf.output_added.context = &e;
  observer_conf.output_added.callback = on_output_port_found;

  libremidi_api_configuration observer_api_conf;
  ret = libremidi_midi_api_configuration_init(&observer_api_conf);
  if (ret != 0)
    return ret;

  observer_api_conf.configuration_type = Observer;
  observer_api_conf.api = ALSA_SEQ;

  libremidi_midi_observer_handle* observer = NULL;
  ret = libremidi_midi_observer_new(&observer_conf, &observer_api_conf, &observer);
  if (ret != 0)
    return ret;

  ret = enumerate_ports(observer, &e);

  if (ret != 0)
  {
    libremidi_midi_observer_free(observer);
    return ret;
  }

  /// Create a real MIDI input
  libremidi_midi_configuration midi_in_conf;
  ret = libremidi_midi_configuration_init(&midi_in_conf);
  if (ret != 0)
    goto free_observer;

  midi_in_conf.version = MIDI1;
  midi_in_conf.in_port = e.in_ports[0];
  midi_in_conf.on_midi1_message.callback = on_midi1_message;

  libremidi_api_configuration midi_in_api_conf;
  ret = libremidi_midi_api_configuration_init(&midi_in_api_conf);
  if (ret != 0)
    goto free_observer;

  midi_in_api_conf.configuration_type = Input;
  midi_in_api_conf.api = ALSA_SEQ;

  libremidi_midi_in_handle* midi_in = NULL;
  ret = libremidi_midi_in_new(&midi_in_conf, &midi_in_api_conf, &midi_in);
  if (ret != 0)
    goto free_observer;

  /// Create a virtual MIDI output
  libremidi_midi_configuration midi_out_conf;
  ret = libremidi_midi_configuration_init(&midi_out_conf);
  if (ret != 0)
    goto free_midi_in;

  midi_out_conf.version = MIDI1;
  midi_out_conf.virtual_port = true;
  midi_out_conf.port_name = "my-app";

  libremidi_api_configuration midi_out_api_conf;
  ret = libremidi_midi_api_configuration_init(&midi_out_api_conf);
  if (ret != 0)
    goto free_midi_in;

  midi_out_api_conf.configuration_type = Output;
  midi_out_api_conf.api = ALSA_SEQ;

  libremidi_midi_out_handle* midi_out = NULL;
  ret = libremidi_midi_out_new(&midi_out_conf, &midi_out_api_conf, &midi_out);
  if (ret != 0)
    goto free_midi_in;

  for (int i = 0; i < 100; i++)
    sleep_ms(1000);

  /// Cleanup
free_midi_out:
  libremidi_midi_out_free(midi_out);

free_midi_in:
  libremidi_midi_in_free(midi_in);

free_observer:
  for (int i = 0; i < e.in_port_count; i++)
    libremidi_midi_in_port_free(e.in_ports[i]);
  for (int i = 0; i < e.out_port_count; i++)
    libremidi_midi_out_port_free(e.out_ports[i]);
  libremidi_midi_observer_free(observer);

  return ret;
}
