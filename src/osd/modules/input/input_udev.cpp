#if !defined(SDLMAME_WIN32)

// MAME headers
#include "emu.h"
#include "osdepend.h"

// MAMEOS headers
#include "input_common.h"

#include <libudev.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>

namespace {

// state information for a lightgun
struct lightgun_state
{
	int32_t lX, lY;
	int32_t buttons[16];
};

struct event_udev_entry
{
   const char *devnode;
   struct udev_list_entry *item;
};

int event_isNumber(const char *s) {
  int n;

  if(strlen(s) == 0) {
    return 0;
  }

  for(n=0; n<strlen(s); n++) {
    if(!(s[n] == '0' || s[n] == '1' || s[n] == '2' || s[n] == '3' || s[n] == '4' ||
         s[n] == '5' || s[n] == '6' || s[n] == '7' || s[n] == '8' || s[n] == '9'))
      return 0;
  }
  return 1;
}

// compare /dev/input/eventX and /dev/input/eventY where X and Y are numbers
int event_strcmp_events(const char* x, const char* y) {

  // find a common string
  int n, common, is_number;
  int a, b;

  n=0;
  while(x[n] == y[n] && x[n] != '\0' && y[n] != '\0') {
    n++;
  }
  common = n;

  // check if remaining string is a number
  is_number = 1;
  if(event_isNumber(x+common) == 0) is_number = 0;
  if(event_isNumber(y+common) == 0) is_number = 0;

  if(is_number == 1) {
    a = atoi(x+common);
    b = atoi(y+common);

    if(a == b) return  0;
    if(a < b)  return -1;
    return 1;
  } else {
    return strcmp(x, y);
  }
}
    
/* Used for sorting devnodes to appear in the correct order */
static int sort_devnodes(const void *a, const void *b)
{
  const struct event_udev_entry *aa = (const struct event_udev_entry*)a;
  const struct event_udev_entry *bb = (const struct event_udev_entry*)b;
  return event_strcmp_events(aa->devnode, bb->devnode);
}

//============================================================
// udev_input_device
//============================================================

class udev_input_device : public event_based_device<struct input_event>
{
public:
  int index;
  udev_input_device(running_machine &machine, std::string &&name, std::string &&id, input_device_class devclass, input_module &module) :
    event_based_device(machine, std::move(name), std::move(id), devclass, module) {
    index = -1;
  }
};

//============================================================
//  udev_lightgun_device
//============================================================

class udev_lightgun_device : public udev_input_device
{
public:
  lightgun_state lightgun;
  int minx, maxx, miny, maxy;

  udev_lightgun_device(running_machine &machine, std::string &&name, std::string &&id, input_module &module) :
    udev_input_device(machine, std::move(name), std::move(id), DEVICE_CLASS_LIGHTGUN, module), lightgun({0}) {
    minx = 0;
    maxx = 0;
    miny = 0;
    maxy = 0;
  }

  void process_event(struct input_event &event) override {
    switch (event.type) {
    case EV_KEY:
      switch (event.code) {
      case BTN_LEFT:
	lightgun.buttons[0] = (event.value == 1) ? 0x80 : 0;
	break;
      case BTN_RIGHT:
	lightgun.buttons[1] = (event.value == 1) ? 0x80 : 0;
	break;
      case BTN_MIDDLE:
	lightgun.buttons[2] = (event.value == 1) ? 0x80 : 0;
	break;
      case BTN_1:
	lightgun.buttons[3] = (event.value == 1) ? 0x80 : 0;
	break;
      case BTN_2:
	lightgun.buttons[4] = (event.value == 1) ? 0x80 : 0;
	break;
      case BTN_3:
	lightgun.buttons[5] = (event.value == 1) ? 0x80 : 0;
	break;
      case BTN_4:
	lightgun.buttons[6] = (event.value == 1) ? 0x80 : 0;
	break;
      case BTN_5:
	lightgun.buttons[7] = (event.value == 1) ? 0x80 : 0;
	break;
      case BTN_6:
	lightgun.buttons[8] = (event.value == 1) ? 0x80 : 0;
	break;
      case BTN_7:
	lightgun.buttons[9] = (event.value == 1) ? 0x80 : 0;
	break;
      case BTN_8:
	lightgun.buttons[10] = (event.value == 1) ? 0x80 : 0;
	break;
      case BTN_9:
	lightgun.buttons[11] = (event.value == 1) ? 0x80 : 0;
	break;
	//case BTN_TOUCH:
	//break;
      default:
	break;
      }
      break;

    //case EV_REL:
    //  switch (event.code) {
    //  case REL_X:
    //	break;
    //  case REL_Y:
    //	break;
    //  case REL_WHEEL:
    //	break;
    //  case REL_HWHEEL:
    //	break;
    //  }
    //  break;

    case EV_ABS:
      switch (event.code) {
      case ABS_X:
	lightgun.lX = normalize_absolute_axis(event.value, minx, maxx);
	break;
      case ABS_Y:
	lightgun.lY = normalize_absolute_axis(event.value, miny, maxy);
	break;
      }
      break;
    }
  }

  void reset() override {
    memset(&lightgun, 0, sizeof(lightgun));
  }
};

//============================================================
//  udev_lightgun_module
//============================================================

class udev_lightgun_module : public input_module_base {

private:
  struct udev *m_udev;
  int m_devices[8];
  int m_ndevices;
    
public:
  udev_lightgun_module() : input_module_base(OSD_LIGHTGUNINPUT_PROVIDER, "udev") {
    m_ndevices = 0;
  }

  ~udev_lightgun_module() {
    for(unsigned int i=0; i<m_ndevices; i++) {
      close(m_devices[i]);
    }
    if (m_udev != NULL) udev_unref(m_udev);
  }

  virtual bool probe() override {
    // udev always available
    return true;
  }

  void input_init(running_machine &machine) override {
    struct udev_enumerate *enumerate;
    struct udev_list_entry     *devs = NULL;
    struct udev_list_entry     *item = NULL;
    unsigned sorted_count = 0;
    struct event_udev_entry sorted[8]; // max devices
    unsigned int i;

    osd_printf_verbose("Lightgun: Begin udev initialization\n");

    m_udev = udev_new();
    if(m_udev == NULL) return;

    enumerate = udev_enumerate_new(m_udev);

    if (enumerate != NULL) {
      udev_enumerate_add_match_property(enumerate, "ID_INPUT_MOUSE", "1");
      udev_enumerate_add_match_subsystem(enumerate, "input");
      udev_enumerate_scan_devices(enumerate);
      devs = udev_enumerate_get_list_entry(enumerate);

      for (item = devs; item; item = udev_list_entry_get_next(item)) {
	const char         *name = udev_list_entry_get_name(item);
	struct udev_device  *dev = udev_device_new_from_syspath(m_udev, name);
	const char      *devnode = udev_device_get_devnode(dev);
	
	if (devnode != NULL && sorted_count < 8) {
	  sorted[sorted_count].devnode = devnode;
	  sorted[sorted_count].item = item;
	  sorted_count++;
	} else {
	  udev_device_unref(dev);
	}
      }

      /* Sort the udev entries by devnode name so that they are
       * created in the proper order */
      qsort(sorted, sorted_count,
	    sizeof(struct event_udev_entry), sort_devnodes);

      for (i = 0; i < sorted_count; i++) {
	const char *name = udev_list_entry_get_name(sorted[i].item);

	/* Get the filename of the /sys entry for the device
	 * and create a udev_device object (dev) representing it. */
	struct udev_device *dev = udev_device_new_from_syspath(m_udev, name);
	const char *devnode     = udev_device_get_devnode(dev);

	if (devnode) {
	  int fd = open(devnode, O_RDONLY | O_NONBLOCK);
	  if (fd != -1) {
	    auto *devinfo = create_lightgun_device(machine, i, fd);
	    if(devinfo != NULL) {
	      struct input_absinfo absx, absy;
	      
	      devinfo->index = m_ndevices;
	      for (int button = 0; button < 16; button++) {
		input_item_id itemid = static_cast<input_item_id>(ITEM_ID_BUTTON1 + button);
		devinfo->device()->add_item(default_button_name(button), itemid, generic_button_get_state<std::int32_t>, &(devinfo->lightgun.buttons[button]));
	      }


	      if (ioctl(fd, EVIOCGABS(ABS_X), &absx) >= 0) {
		if (ioctl(fd, EVIOCGABS(ABS_Y), &absy) >= 0) {
		  devinfo->minx = absx.minimum;
		  devinfo->maxx = absx.maximum;
		  devinfo->miny = absy.minimum;
		  devinfo->maxy = absy.maximum;

		  devinfo->device()->add_item("axis X", ITEM_ID_XAXIS, generic_axis_get_state<std::int32_t>, &(devinfo->lightgun.lX));
		  devinfo->device()->add_item("axis Y", ITEM_ID_YAXIS, generic_axis_get_state<std::int32_t>, &(devinfo->lightgun.lY));
	      
		  m_devices[m_ndevices++] = fd;
		}
	      }
	    }
	  }
	}
	udev_device_unref(dev);
      }
      udev_enumerate_unref(enumerate);
    }

    osd_printf_verbose("Lightgun: End udev initialization\n");
  }

  bool should_poll_devices(running_machine &machine) override {
    return true;
  }

  void before_poll(running_machine &machine) override {
    struct input_event input_events[32];
    int j, len;

    if (!should_poll_devices(machine))
      return;

    for(unsigned int i=0; i<m_ndevices; i++) {
      while ((len = read(m_devices[i], input_events, sizeof(input_events))) > 0) {
	len /= sizeof(*input_events);
	for (j = 0; j < len; j++) {
	  handle_event(i, input_events[j]);
	}
      }
    }
  }

  void handle_event(int index, struct input_event& e) {
    // Figure out which lightgun this event id destined for
    auto target_device = std::find_if(devicelist().begin(), devicelist().end(), [index](auto &device)
    {
      std::unique_ptr<device_info> &ptr = device;
      return downcast<udev_input_device*>(ptr.get())->index == index;
    });

    // If we find a matching lightgun, dispatch the event to the lightgun
    if (target_device != devicelist().end()) {
      downcast<udev_input_device*>((*target_device).get())->queue_events(&e, 1);
    }

    downcast<udev_input_device*>((*target_device).get())->queue_events(&e, 1);
  }

private:
  udev_lightgun_device *create_lightgun_device(running_machine &machine, int index, int fd) {
    char name[64];

    if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) < 0) {
      name[0] = '\0';
    }
    return &devicelist().create_device<udev_lightgun_device>(machine, name, name, *this);
  }
};

} // anonymous namespace

#else // !defined(SDLMAME_WIN32)

MODULE_NOT_SUPPORTED(udev_lightgun_module, OSD_LIGHTGUNINPUT_PROVIDER, "udev")

#endif // !defined(SDLMAME_WIN32)

MODULE_DEFINITION(LIGHTGUN_UDEV, udev_lightgun_module)
