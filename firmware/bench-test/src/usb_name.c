// The name the controller gives the computer, so the app knows it without being taught:
// it shows up in the app's Device check tab as "Gig Rig Controller" (in place of "Teensy MIDI").
// The app looks for these words (src/KickPad.cpp, looksLikeController): do not change them.

#include <usb_names.h>

#define PRODUCT_NAME      { 'G','i','g',' ','R','i','g',' ','C','o','n','t','r','o','l','l','e','r' }
#define PRODUCT_NAME_LEN  18

struct usb_string_descriptor_struct usb_string_product_name = {
    2 + PRODUCT_NAME_LEN * 2,
    3,
    PRODUCT_NAME
};
