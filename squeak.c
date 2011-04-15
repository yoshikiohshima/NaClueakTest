/** @file squeak.c
 * This example demonstrates loading, running and scripting a very simple
 * NaCl module.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ppapi/c/dev/ppb_var_deprecated.h>
#include <ppapi/c/dev/ppp_class_deprecated.h>
#include <ppapi/c/pp_errors.h>
#include <ppapi/c/pp_var.h>
#include <ppapi/c/pp_module.h>
#include <ppapi/c/ppb.h>
#include <ppapi/c/ppb_instance.h>
#include <ppapi/c/ppp.h>
#include <ppapi/c/ppp_instance.h>
#include <ppapi/c/pp_size.h>
#include <ppapi/c/pp_rect.h>
#include <ppapi/c/pp_point.h>
#include <ppapi/c/ppb_image_data.h>
#include <ppapi/c/ppb_graphics_2d.h>
#include <ppapi/c/ppb_core.h>
#include <ppapi/c/pp_input_event.h>
#include <ppapi/c/pp_completion_callback.h>

static PP_Bool Instance_DidCreate(PP_Instance instance,
                                  uint32_t argc,
                                  const char* argn[],
                                  const char* argv[]);
static void Instance_DidDestroy(PP_Instance instance);
static void Instance_DidChangeView(PP_Instance instance,
                                   const struct PP_Rect* position,
                                   const struct PP_Rect* clip);
static void Instance_DidChangeFocus(PP_Instance instance,
                                    PP_Bool has_focus);
static PP_Bool Instance_HandleInputEvent(PP_Instance instance,
                                         const struct PP_InputEvent* event);
static struct PP_Var Instance_GetInstanceObject(PP_Instance instance);

void DestroyContext(PP_Instance instance);
void CreateContext(PP_Instance instance, const struct PP_Size* size);
void FlushPixelBuffer();
struct PP_Var Paint();

static struct PPB_Var_Deprecated* var_interface = NULL;
static struct PPP_Class_Deprecated ppp_class;
static PP_Module module_id = 0;
const struct PPB_Graphics2D* graphics_2d_;
const struct PPB_ImageData* image_data_;
const struct PPB_Core* core_;
const struct PPB_Instance* instance_;

PP_Resource gc;
PP_Resource image;

const char* const kPaintMethodId = "paint";
const char* const kGetStatusMethodId = "getStatus";

static int32_t flush_pending;

static char status[10000];
static char buffer[1024];

static struct PPP_Instance instance_interface = {
  &Instance_DidCreate,
  &Instance_DidDestroy,
  &Instance_DidChangeView,
  &Instance_DidChangeFocus,
  &Instance_HandleInputEvent,
  NULL,  /* HandleDocumentLoad is not supported by NaCl modules. */
  &Instance_GetInstanceObject,
};

static struct PP_ImageDataDesc desc;

static PP_Bool
getDesc()
{
  if (!image) {
    return false;
  }

  return image_data_->Describe(image, &desc);
}

static int32_t
width()
{
  return getDesc() ? desc.size.width : 0;
}

static int32_t
height()
{
  return getDesc() ? desc.size.height : 0;
}

/**
 * Returns C string contained in the @a var or NULL if @a var is not string.
 * @param[in] var PP_Var containing string.
 * @return a C string representation of @a var.
 * @note Returned pointer will be invalid after destruction of @a var.
 */
static const char* VarToCStr(struct PP_Var var) {
  uint32_t len = 0;
  if (NULL != var_interface)
    return var_interface->VarToUtf8(var, &len);
  return NULL;
}

/**
 * Creates new string PP_Var from C string. The resulting object will be a
 * refcounted string object. It will be AddRef()ed for the caller. When the
 * caller is done with it, it should be Release()d.
 * @param[in] str C string to be converted to PP_Var
 * @return PP_Var containing string.
 */
static struct PP_Var StrToVar(const char* str) {
  if (NULL != var_interface)
    return var_interface->VarFromUtf8(module_id, str, strlen(str));
  return PP_MakeUndefined();
}

static PP_Bool Instance_DidCreate(PP_Instance instance,
                                  uint32_t argc,
                                  const char* argn[],
                                  const char* argv[]) {
  strcat(status, "create\n");
  return PP_TRUE;
}

static void Instance_DidDestroy(PP_Instance instance) {
}

static void Instance_DidChangeView(PP_Instance instance,
                                   const struct PP_Rect* position,
                                   const struct PP_Rect* clip) {
  strcat(status, "change view\n");
  if (position->size.width == width() &&
      position->size.height == height())
    return;  // Size didn't change, no need to update anything.
  DestroyContext(instance);
  CreateContext(instance, &position->size);
  strcat(status, graphics_2d_->IsGraphics2D(gc) ? "gc ok\n" : "gc ng\n");
  core_->ReleaseResource(image);
  if (1 /*graphics_2d_->IsGraphics2D(gc)*/) {
    strcat(status, "make image\n");
    uint32_t *pixels;
    int32_t i, j;
    struct PP_Size s = position->size;
    image = image_data_->Create(instance, PP_IMAGEDATAFORMAT_BGRA_PREMUL,
				&s,
				PP_FALSE);
    getDesc();
    sprintf(buffer, "desc %d, %d, %d, %d\n", (int)desc.format, (int)desc.size.width, (int)desc.size.height, (int)desc.stride);
    strcat(status, buffer);
    pixels = image_data_->Map(image);
    for (j = 0; j < height(); j++) {
      for (i = 0; i < width(); i++) {
	pixels[j*(desc.stride/4)+i] = 0x7FFF7FFF;
      }
    }
    image_data_->Unmap(image);
  }
}

static void Instance_DidChangeFocus(PP_Instance instance,
                                    PP_Bool has_focus) {
}

static PP_Bool Instance_HandleInputEvent(PP_Instance instance,
                                         const struct PP_InputEvent* event) {
  if (event->type == PP_INPUTEVENT_TYPE_MOUSEDOWN) {
    uint32_t *pixels;
    int32_t i, j;
    unsigned char c = event->u.mouse.x;
    uint32_t w = (c << 24) + (c << 16) + (c << 8) + 255;
    sprintf(buffer, "event pos: %d, %d\n", (int)event->u.mouse.x, (int)event->u.mouse.y);
    strcat(status, buffer);
    pixels = image_data_->Map(image);
    for (j = 0; j < height(); j++) {
      for (i = 0; i < width(); i++) {
	pixels[j*(desc.stride/4)+i] = w;
      }
    }
    image_data_->Unmap(image);
  }
    return PP_TRUE;
}

/**
 * Create scriptable object for the given instance.
 * @param[in] instance The instance ID.
 * @return A scriptable object.
 */
static struct PP_Var Instance_GetInstanceObject(PP_Instance instance) {
  if (var_interface)
    return var_interface->CreateObject(instance, &ppp_class, NULL);
  return PP_MakeUndefined();
}

/**
 * Check existence of the function associated with @a name.
 * @param[in] object unused
 * @param[in] name method name
 * @param[out] exception pointer to the exception object, unused
 * @return If the method does exist, return true.
 * If the method does not exist, return false and don't set the exception.
 */
static bool Squeak_HasMethod(void* object,
                                 struct PP_Var name,
                                 struct PP_Var* exception) {

  const char* method_name = VarToCStr(name);
  if (NULL != method_name) {
    if (strcmp(method_name, kPaintMethodId) == 0)
      return true;
    if (strcmp(method_name, kGetStatusMethodId) == 0)
      return true;
  }
  return false;
}

/**
 * Invoke the function associated with @a name.
 * @param[in] object unused
 * @param[in] name method name
 * @param[in] argc number of arguments
 * @param[in] argv array of arguments
 * @param[out] exception pointer to the exception object, unused
 * @return If the method does exist, return true.
 */
static struct PP_Var Squeak_Call(void* object,
				 struct PP_Var name,
				 uint32_t argc,
				 struct PP_Var* argv,
				 struct PP_Var* exception) {
  struct PP_Var v = PP_MakeInt32(0);
  const char* method_name = VarToCStr(name);
  if (NULL != method_name) {
    if (strcmp(method_name, kPaintMethodId) == 0)
      Paint();
    if (strcmp(method_name, kGetStatusMethodId) == 0)
      return StrToVar(status);
  }
  return v;
}

/**
 * Entrypoints for the module.
 * Initialize instance interface and scriptable object class.
 * @param[in] a_module_id module ID
 * @param[in] get_browser pointer to PPB_GetInterface
 * @return PP_OK on success, any other value on failure.
 */

PP_EXPORT int32_t PPP_InitializeModule(PP_Module a_module_id,
                                       PPB_GetInterface get_browser_interface) {
  module_id = a_module_id;
  var_interface = 
      (struct PPB_Var_Deprecated*)(get_browser_interface(PPB_VAR_DEPRECATED_INTERFACE));
  core_ = (const struct PPB_Core*)
    get_browser_interface(PPB_CORE_INTERFACE);
  instance_ = (const struct PPB_Instance*)
    get_browser_interface(PPB_INSTANCE_INTERFACE);
  graphics_2d_ = (const struct PPB_Graphics2D*)
    get_browser_interface(PPB_GRAPHICS_2D_INTERFACE);
  image_data_ = (const struct PPB_ImageData*)
    get_browser_interface(PPB_IMAGEDATA_INTERFACE);
  memset(&ppp_class, 0, sizeof(ppp_class));
  ppp_class.Call = Squeak_Call;
  ppp_class.HasMethod = Squeak_HasMethod;
  strcat(status, "initialize module");
  return PP_OK;
}

/**
 * Returns an interface pointer for the interface of the given name, or NULL
 * if the interface is not supported.
 * @param[in] interface_name name of the interface
 * @return pointer to the interface
 */
PP_EXPORT const void* PPP_GetInterface(const char* interface_name) {
  if (strcmp(interface_name, PPP_INSTANCE_INTERFACE) == 0)
    return &instance_interface;
  return NULL;
}

/**
 * Called before the plugin module is unloaded.
 */
PP_EXPORT void PPP_ShutdownModule() {
}

void
DestroyContext(PP_Instance instance) {
  strcat(status, graphics_2d_->IsGraphics2D(gc) ? "destroy good\n" : "destroy bad\n");
  if (!(graphics_2d_->IsGraphics2D(gc)))
    return;
  core_->ReleaseResource(gc);
}

void
CreateContext(PP_Instance instance, const struct PP_Size* size) {
  if (graphics_2d_->IsGraphics2D(gc))
    return;
  strcat(status, "making gc\n");
  sprintf(buffer, "size: %d, %d\n", (int)size->width, (int)size->height);
  strcat(status, buffer);
  gc = graphics_2d_->Create(instance, size, false);
  sprintf(buffer, "gc: %d\n", (int)gc);
  strcat(status, buffer);
  if (!instance_->BindGraphics(instance, gc)) {
    strcat(status, "couldn't bind gc\n");
  }
}

static void
FlushCallback(void *user_data, int32_t result)
{
  strcat(status, "flush complete\n");
  flush_pending = 0;
}

static struct PP_CompletionCallback CompletionCallback = {FlushCallback, 0};

void
FlushPixelBuffer() {
  struct PP_Point top_left;
  /*  struct PP_Rect src_left = NULL;*/
  top_left.x = 0;
  top_left.y = 0;
  strcat(status, "flush 1\n");
  if (0 /*!(graphics_2d_->IsGraphics2D(gc))*/) {
    strcat(status, "flush 2\n");
    flush_pending = 0;
    return;
  }
    strcat(status, "flush 3\n");
  graphics_2d_->PaintImageData(gc, image, &top_left, NULL);
  flush_pending = 1;
  graphics_2d_->Flush(gc, CompletionCallback);
}

struct PP_Var
Paint() {
  strcat(status, "paint 1\n");
  if (!flush_pending) {
    strcat(status, "paint 2\n");
    FlushPixelBuffer();
  }
  return StrToVar(status);
}
