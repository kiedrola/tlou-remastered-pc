/* The shake input (src/runtime_pad.c play_shake): a held shake source makes the motion sensors
 * report a controller shaken by hand, which The Last of Us Remastered needs to fix a flickering
 * flashlight. Sources: the pad file's "shake", a gamepad's Share/Capture button (MISC1) and a
 * DirectInput controller's button chosen in GOW3_DINPUT_BUTTONS. The DirectInput controller's
 * sticks, hat and buttons are checked on the way. Only SDL's virtual controllers are seen: the
 * real joystick drivers are off. */
#define _GNU_SOURCE
#include <assert.h>
#include <unistd.h>
#include "../src/runtime_pad.c"
#ifdef _WIN32
#include <windows.h>
#define setenv(name, value, overwrite) _putenv_s(name, value)
#endif

int gow3gpu_overlay_captures_input(void) { return 0; }
uintptr_t runtime_lookup(const RuntimeExport *table, size_t count, const char *name) {
    (void)table; (void)count; (void)name;
    return 0;
}
/* The rest of the game the pad talks to: the default controls with the mouse off, nothing else. */
unsigned gow3gpu_controls_generation(void) { return 1; }
void gow3gpu_controls_get(Gow3Controls *out) {
    memset(out,0,sizeof(*out));
    out->prompts=-1; out->mouse_sens=1.0f; out->stick_speed=1.0f; out->walk_speed=0.75f;
}
float gow3gpu_controls_mouse_aim_sens(void) { return 1.0f; }
float gow3gpu_controls_mouse_vertical(void) { return 1.0f; }
int gow3gpu_controls_mouse_invert_x(void) { return 0; }
int gow3gpu_controls_vibration(void) { return 1; }
int gow3gpu_mouse_take(float *dx, float *dy, unsigned *buttons) { *dx=*dy=0; if (buttons) *buttons=0; return 0; }
unsigned gow3gpu_mouse_look_calls(void) { return 0; }
void gow3gpu_mouse_look_pause(int paused) { (void)paused; }
void gow3gpu_mouse_inject(float dx, float dy) { (void)dx; (void)dy; }
void gow3gpu_prompts_set_labels(int set, const char *const *labels, int count) { (void)set; (void)labels; (void)count; }
void gow3gpu_prompts_set_device(int device) { (void)device; }
void runtime_menu_exit_tick(void) {}
void runtime_gamesettings_tick(void) {}
void runtime_mouselook_set_stick_boost(float boost) { (void)boost; }
int runtime_mouselook_camera_recent(void) { return 0; }
int runtime_mouselook_aim_recent(void) { return 0; }

static void inject(const char *path, const char *tokens) {
    FILE *f=fopen(path,"w");
    assert(f);
    fputs(tokens,f);
    fclose(f);
    usleep(25000);
}
static PadData read_pad(void) {
    PadData d;
    assert(pad_read_state(1,&d)==0);
    return d;
}
static void pump(void) { SDL_UpdateJoysticks(); SDL_UpdateGamepads(); }
static float magnitude(const float v[3]) { return fabsf(v[0])+fabsf(v[1])+fabsf(v[2]); }
static int sensors_at_rest(const PadData *d) {
    return magnitude(d->acceleration)==0.0f && magnitude(d->angular_velocity)==0.0f;
}
/* Reads for about 150 ms (more than one period of the 8 Hz wave): the largest acceleration and
 * rotation seen, and whether the shake bit ever reached the game. `extra` must be held on every
 * read. */
static void shaken(const char *what, uint32_t extra) {
    float accel=0, rotation=0;
    for (int i=0;i<15;++i) {
        pump();
        const PadData d=read_pad();
        assert(!(d.buttons & PAD_SHAKE));
        assert((d.buttons & extra)==extra);
        assert(fabsf(d.acceleration[0])<=2.5f+1e-4f && fabsf(d.acceleration[2])<=1.5f+1e-4f);
        if (magnitude(d.acceleration)>accel) accel=magnitude(d.acceleration);
        if (magnitude(d.angular_velocity)>rotation) rotation=magnitude(d.angular_velocity);
        usleep(10000);
    }
    printf("  %s: largest acceleration %.2f g, rotation %.2f rad/s\n",what,accel,rotation);
    assert(accel>2.0f && rotation>5.0f);
}

/* Reads `reads` times, 10 ms apart, after step(i) moves the controller: how many reads had the
 * sensors shaken (the shake bit never reaches the game). */
static SDL_Joystick *stepped;
static int count_shaken(void (*step)(int), int reads) {
    int shaken_reads=0;
    for (int i=0;i<reads;++i) {
        if (step) step(i);
        pump();
        const PadData d=read_pad();
        assert(!(d.buttons & PAD_SHAKE));
        if (!sensors_at_rest(&d)) ++shaken_reads;
        usleep(10000);
    }
    return shaken_reads;
}
static void axis6_swings(int i) { assert(SDL_SetJoystickVirtualAxis(stepped,6,i%2 ? SDL_JOYSTICK_AXIS_MAX : SDL_JOYSTICK_AXIS_MIN)); }
static void axis6_drifts(int i) { assert(SDL_SetJoystickVirtualAxis(stepped,6,(Sint16)(i*200))); }
static void accel(float x, float y, float z) {
    const float v[3]={x,y,z};
    assert(SDL_SendJoystickVirtualSensorData(stepped,SDL_SENSOR_ACCEL,SDL_GetTicksNS(),v,3));
}
static void accel_gravity(int i) { (void)i; accel(0,SDL_STANDARD_GRAVITY,0); }
static void accel_fixed_clone(int i) { (void)i; accel(0,22.217f,0); }   /* a PS3 clone's fixed reading */
static void accel_tilts(int i) {   /* turned slowly: still 1 g long */
    const float a=0.05f*(float)i;
    accel(SDL_STANDARD_GRAVITY*sinf(a),SDL_STANDARD_GRAVITY*cosf(a),0);
}
static void accel_shaken(int i) { accel(i%2 ? 2.5f*SDL_STANDARD_GRAVITY : -1.5f*SDL_STANDARD_GRAVITY,SDL_STANDARD_GRAVITY,0); }
/* Shaken sideways by hand: gravity stays on y, x swings by 1 g each way (its length moves only
 * 0.41 g: the first version, which watched the length, missed this). */
static void accel_sideways(int i) { accel(i%2 ? SDL_STANDARD_GRAVITY : -SDL_STANDARD_GRAVITY,SDL_STANDARD_GRAVITY,0); }

int main(void) {
    setvbuf(stdout,NULL,_IONBF,0);
    char path[1024];
#ifdef _WIN32
    /* Windows' own temporary folder: an MSYS shell may pass no TEMP, and its TMPDIR (/tmp) means
     * nothing to a Windows program. */
    char tmp[MAX_PATH+1];
    const DWORD length=GetTempPathA(sizeof(tmp),tmp);
    assert(length>0 && length<sizeof(tmp));
    tmp[length-1]=0;   /* without the trailing backslash */
#else
    const char *tmp=getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp";
#endif
    /* (mkstemp fails on Windows here: a name with the process id instead.) */
    snprintf(path,sizeof(path),"%s/gow3-pad-shake-%d.txt",tmp,(int)getpid());
    FILE *created=fopen(path,"w");
    if (!created) { fprintf(stderr,"Cannot create %s: %s\n",path,strerror(errno)); return 1; }
    fclose(created);
    setenv("GOW3_PAD_FILE",path,1);
    setenv("GOW3_AUTO_SHAKE","0",1);
    setenv("GOW3_KEYBOARD","0",1);
    setenv("GOW3_MOUSE","0",1);
    setenv("SDL_VIDEODRIVER","dummy",1);
    /* Axes 4 and 5 are the triggers (resting in the middle here: pressed past it), 6 shakes. */
    setenv("GOW3_DINPUT_AXES","0,1,2,3,4,5,6",1);
    /* The PS button (12) shakes, the rest is the default layout. */
    setenv("GOW3_DINPUT_BUTTONS","square,cross,circle,triangle,l1,r1,l2,r2,touchpad_right,options,l3,r3,shake,"
                                 "touchpad_left",1);
    /* Only the virtual controllers below, whatever is plugged in. */
    const char *drivers[]={SDL_HINT_JOYSTICK_DIRECTINPUT,SDL_HINT_XINPUT_ENABLED,SDL_HINT_JOYSTICK_RAWINPUT,
                           SDL_HINT_JOYSTICK_HIDAPI,SDL_HINT_JOYSTICK_WGI,SDL_HINT_JOYSTICK_GAMEINPUT};
    for (size_t i=0;i<sizeof(drivers)/sizeof(*drivers);++i) SDL_SetHint(drivers[i],"0");
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT,"0x1d50/0x6189");
    assert(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMEPAD));
    assert(pad_init()==0 && pad_open(1,0,0,NULL)==1);

    /* At rest the sensors read zero. */
    PadData data=read_pad();
    assert(sensors_at_rest(&data) && data.buttons==0);

    puts("Pad file:");
    inject(path,"shake");
    shaken("shake",0);
    inject(path,"shake cross");
    shaken("shake + cross",BTN_CROSS);
    inject(path,"");
    data=read_pad();
    assert(sensors_at_rest(&data) && data.buttons==0);

    puts("DirectInput controller (no gamepad mapping):");
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type=SDL_JOYSTICK_TYPE_UNKNOWN;
    desc.naxes=7;
    desc.nhats=1;
    desc.nbuttons=14;
    desc.name="gow3 test DirectInput pad";
    desc.vendor_id=0x1234;
    desc.product_id=0x5678;
    SDL_JoystickID dinput_id=SDL_AttachVirtualJoystick(&desc);
    assert(dinput_id!=0 && !SDL_IsGamepad(dinput_id));
    SDL_Joystick *dinput=SDL_OpenJoystick(dinput_id);
    assert(dinput);
    pump();
    data=read_pad();
    assert(joystick && !gamepad && sensors_at_rest(&data) && data.buttons==0);
    assert(data.left_x==128 && data.left_y==128 && data.right_x==128 && data.right_y==128);
    /* Axes 0-3: left x, left y, right x, right y; the hat is the d-pad. */
    assert(SDL_SetJoystickVirtualAxis(dinput,0,SDL_JOYSTICK_AXIS_MAX));
    assert(SDL_SetJoystickVirtualAxis(dinput,1,SDL_JOYSTICK_AXIS_MIN));
    assert(SDL_SetJoystickVirtualAxis(dinput,3,SDL_JOYSTICK_AXIS_MAX));
    assert(SDL_SetJoystickVirtualHat(dinput,0,SDL_HAT_LEFTUP));
    assert(SDL_SetJoystickVirtualButton(dinput,1,true));    /* cross */
    assert(SDL_SetJoystickVirtualButton(dinput,6,true));    /* l2 */
    pump();
    data=read_pad();
    assert(data.left_x==255 && data.left_y==0 && data.right_x==128 && data.right_y==255);
    assert(data.buttons==(BTN_UP|BTN_LEFT|BTN_CROSS|BTN_L2) && data.l2==255);
    assert(sensors_at_rest(&data));
    /* The R2 axis pushed past the threshold presses R2 (in full, as a gamepad's trigger does);
     * a touch does not. */
    assert(SDL_SetJoystickVirtualAxis(dinput,5,SDL_JOYSTICK_AXIS_MAX/2));
    pump();
    data=read_pad();
    assert(data.r2==255 && (data.buttons & BTN_R2));
    assert(SDL_SetJoystickVirtualAxis(dinput,5,2000));
    pump();
    data=read_pad();
    assert(data.r2<30 && !(data.buttons & BTN_R2));
    assert(SDL_SetJoystickVirtualAxis(dinput,5,0));
    assert(SDL_SetJoystickVirtualAxis(dinput,0,0) && SDL_SetJoystickVirtualAxis(dinput,1,0) &&
           SDL_SetJoystickVirtualAxis(dinput,3,0));
    assert(SDL_SetJoystickVirtualHat(dinput,0,SDL_HAT_CENTERED));
    assert(SDL_SetJoystickVirtualButton(dinput,1,false) && SDL_SetJoystickVirtualButton(dinput,6,false));
    /* Button 12 (PS) shakes it, as GOW3_DINPUT_BUTTONS says. */
    assert(SDL_SetJoystickVirtualButton(dinput,12,true));
    shaken("button 12",0);
    assert(SDL_SetJoystickVirtualButton(dinput,12,false));
    pump();
    data=read_pad();
    assert(sensors_at_rest(&data) && data.buttons==0);
    /* Axis 6 (its shake axis) swung back and forth shakes it; drifting slowly or held does not. */
    stepped=dinput;
    int n=count_shaken(axis6_drifts,20);
    printf("  axis 6 drifting: %d of 20 reads shaken\n",n);
    assert(n==0);
    n=count_shaken(axis6_swings,20);
    printf("  axis 6 swung: %d of 20 reads shaken\n",n);
    assert(n>=10);
    usleep(500000);
    n=count_shaken(NULL,10);
    printf("  axis 6 held after: %d of 10 reads shaken\n",n);
    assert(n==0);
    SDL_CloseJoystick(dinput);
    assert(SDL_DetachVirtualJoystick(dinput_id));
    pump();
    data=read_pad();
    assert(!joystick);

    puts("Gamepad:");
    SDL_INIT_INTERFACE(&desc);
    desc.type=SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes=SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons=SDL_GAMEPAD_BUTTON_COUNT;
    desc.button_mask=(1u<<SDL_GAMEPAD_BUTTON_COUNT)-1;
    desc.axis_mask=(1u<<SDL_GAMEPAD_AXIS_COUNT)-1;
    desc.name="gow3 test controller";
    desc.vendor_id=0x1d50;
    desc.product_id=0x6189;
    SDL_VirtualJoystickSensorDesc sensor={SDL_SENSOR_ACCEL,100.0f};
    desc.nsensors=1;
    desc.sensors=&sensor;
    SDL_JoystickID pad_id=SDL_AttachVirtualJoystick(&desc);
    assert(pad_id!=0 && SDL_IsGamepad(pad_id));
    SDL_Joystick *pad=SDL_OpenJoystick(pad_id);
    assert(pad);
    pump();
    data=read_pad();
    assert(gamepad && !joystick && sensors_at_rest(&data));
    /* Share/Capture (MISC1) shakes it, with other buttons held. */
    assert(SDL_SetJoystickVirtualButton(pad,SDL_GAMEPAD_BUTTON_MISC1,true));
    assert(SDL_SetJoystickVirtualButton(pad,SDL_GAMEPAD_BUTTON_SOUTH,true));
    shaken("Share/Capture + south",BTN_CROSS);
    assert(SDL_SetJoystickVirtualButton(pad,SDL_GAMEPAD_BUTTON_MISC1,false));
    assert(SDL_SetJoystickVirtualButton(pad,SDL_GAMEPAD_BUTTON_SOUTH,false));
    pump();
    data=read_pad();
    assert(sensors_at_rest(&data) && data.buttons==0);
    /* Its accelerometer: shaken by hand it shakes; at rest, turned slowly or stuck at one value
     * (a clone's fake sensor) it does not. */
    stepped=pad;
    const struct { const char *what; void (*step)(int); int shakes; } moves[]={
        {"at rest (1 g)",accel_gravity,0}, {"fixed 22.2 m/s2 (clone)",accel_fixed_clone,0},
        {"turned slowly",accel_tilts,0}, {"shaken",accel_shaken,1}, {"shaken sideways",accel_sideways,1},
    };
    for (size_t i=0;i<sizeof(moves)/sizeof(*moves);++i) {
        usleep(500000);   /* the previous shake's hold ends */
        n=count_shaken(moves[i].step,20);
        printf("  accelerometer %s: %d of 20 reads shaken\n",moves[i].what,n);
        assert(moves[i].shakes ? n>=10 : n==0);
    }
    SDL_CloseJoystick(pad);
    if (gamepad) SDL_CloseGamepad(gamepad);
    gamepad=NULL;
    assert(SDL_DetachVirtualJoystick(pad_id));
    SDL_Quit();
    unlink(path);
    puts("PASS: shake from the pad file, a DirectInput button and axis, Share/Capture and an accelerometer; DirectInput sticks, hat, "
         "buttons; sensors at rest otherwise");
}
