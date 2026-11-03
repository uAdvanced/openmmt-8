/* macOS panel window (Cocoa, OpenGL 3.2). This file has not been built or run on a Mac. */
#include "gui.h"

#define GL_SILENCE_DEPRECATION
#import <Cocoa/Cocoa.h>
#import <OpenGL/gl3.h>

namespace mmt8 {
class CocoaGui;
}

@interface OpenMMT8PanelView : NSOpenGLView
- (instancetype)initWithFrame:(NSRect)frame pixelFormat:(NSOpenGLPixelFormat *)format owner:(mmt8::CocoaGui *)owner;
- (void)detach;
@end

#include <string>

#include "builtin_rom.h"
#include "engine.h"
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "log.h"
#include "panel_ui.h"

namespace mmt8 {

class CocoaGui final : public Gui {
public:
    CocoaGui(Engine &engine, GuiHost host) : engine_(engine), host_(std::move(host)), panel_(engine_, host_) {}
    ~CocoaGui() override { destroy(); }

    bool set_parent(void *native) override;
    void set_scale(double scale) override { scale_ = scale > 0.25 ? scale : 1.0; }
    double scale() const override { return scale_ > 0.0 ? scale_ : 1.0; }
    void set_size(uint32_t w, uint32_t h) override;
    void show(bool visible) override;

    void frame();
    void mouse_move(float x, float y);
    void mouse_button(float x, float y, int button, bool down);
    void mouse_wheel(float dx, float dy);
    void key(unsigned short code, bool down, NSString *chars, bool command);
    void focus_lost();

private:
    bool create_gl(NSView *parent);
    void destroy();
    void pick_rom();

    Engine &engine_;
    GuiHost host_;
    PanelUi panel_;
    OpenMMT8PanelView *view_ = nil;
    NSTimer *timer_ = nil;
    ImGuiContext *ctx_ = nullptr;
    uint32_t width_ = kDefaultWidth;
    uint32_t height_ = height_for(kDefaultWidth);
    double scale_ = 1.0;
    bool in_modal_ = false;
};

}  // namespace mmt8

@implementation OpenMMT8PanelView {
    mmt8::CocoaGui *owner_;
    NSTrackingArea *tracking_;
}
- (instancetype)initWithFrame:(NSRect)frame pixelFormat:(NSOpenGLPixelFormat *)format owner:(mmt8::CocoaGui *)owner {
    self = [super initWithFrame:frame pixelFormat:format];
    if (self) owner_ = owner;
    return self;
}
- (void)detach { owner_ = nil; }
- (BOOL)isOpaque { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (void)tick:(NSTimer *)timer {
    (void)timer;
    if (owner_) owner_->frame();
}
- (void)updateTrackingAreas {
    [super updateTrackingAreas];
    if (tracking_) [self removeTrackingArea:tracking_];
    tracking_ = [[NSTrackingArea alloc]
        initWithRect:NSZeroRect
             options:NSTrackingMouseEnteredAndExited | NSTrackingMouseMoved | NSTrackingActiveInActiveApp |
                     NSTrackingInVisibleRect | NSTrackingEnabledDuringMouseDrag
               owner:self
            userInfo:nil];
    [self addTrackingArea:tracking_];
}
- (NSPoint)local:(NSEvent *)event {
    NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    if (![self isFlipped]) p.y = self.bounds.size.height - p.y;
    return p;
}
- (void)mouseMoved:(NSEvent *)event {
    if (!owner_) return;
    NSPoint p = [self local:event];
    owner_->mouse_move(static_cast<float>(p.x), static_cast<float>(p.y));
}
- (void)mouseDragged:(NSEvent *)event { [self mouseMoved:event]; }
- (void)rightMouseDragged:(NSEvent *)event { [self mouseMoved:event]; }
- (void)otherMouseDragged:(NSEvent *)event { [self mouseMoved:event]; }
- (void)mouseExited:(NSEvent *)event {
    (void)event;
    if (owner_) owner_->mouse_move(-FLT_MAX, -FLT_MAX);
}
- (void)mouseDown:(NSEvent *)event { [self button:event index:0 down:YES]; }
- (void)mouseUp:(NSEvent *)event { [self button:event index:0 down:NO]; }
- (void)rightMouseDown:(NSEvent *)event { [self button:event index:1 down:YES]; }
- (void)rightMouseUp:(NSEvent *)event { [self button:event index:1 down:NO]; }
- (void)otherMouseDown:(NSEvent *)event { [self button:event index:2 down:YES]; }
- (void)otherMouseUp:(NSEvent *)event { [self button:event index:2 down:NO]; }
- (void)button:(NSEvent *)event index:(int)index down:(BOOL)down {
    if (!owner_) return;
    if (down) [self.window makeFirstResponder:self];
    NSPoint p = [self local:event];
    owner_->mouse_button(static_cast<float>(p.x), static_cast<float>(p.y), index, down);
}
- (void)scrollWheel:(NSEvent *)event {
    if (owner_) owner_->mouse_wheel(static_cast<float>(event.scrollingDeltaX), static_cast<float>(event.scrollingDeltaY));
}
- (void)keyDown:(NSEvent *)event { [self key:event down:YES]; }
- (void)keyUp:(NSEvent *)event { [self key:event down:NO]; }
- (void)key:(NSEvent *)event down:(BOOL)down {
    if (!owner_) return;
    const bool command = (event.modifierFlags & NSEventModifierFlagCommand) != 0;
    owner_->key(event.keyCode, down, down ? event.characters : nil, command);
}
- (void)flagsChanged:(NSEvent *)event {
    if (!owner_) return;
    const bool shift = (event.modifierFlags & NSEventModifierFlagShift) != 0;
    owner_->key(event.keyCode, shift, nil, false);
}
- (BOOL)resignFirstResponder {
    const BOOL ok = [super resignFirstResponder];
    if (ok && owner_) owner_->focus_lost();
    return ok;
}
- (void)reshape {
    [super reshape];
    [[self openGLContext] update];
}
@end

namespace mmt8 {
namespace {

/* Virtual key codes from HIToolbox/Events.h. Only the keys the panel and the text fields use. */
ImGuiKey map_key(unsigned short code) {
    switch (code) {
        case 0x1D: return ImGuiKey_0;
        case 0x12: return ImGuiKey_1;
        case 0x13: return ImGuiKey_2;
        case 0x14: return ImGuiKey_3;
        case 0x15: return ImGuiKey_4;
        case 0x17: return ImGuiKey_5;
        case 0x16: return ImGuiKey_6;
        case 0x1A: return ImGuiKey_7;
        case 0x1C: return ImGuiKey_8;
        case 0x19: return ImGuiKey_9;
        case 0x1B: return ImGuiKey_Minus;
        case 0x18: return ImGuiKey_Equal;
        case 0x52: return ImGuiKey_Keypad0;
        case 0x53: return ImGuiKey_Keypad1;
        case 0x54: return ImGuiKey_Keypad2;
        case 0x55: return ImGuiKey_Keypad3;
        case 0x56: return ImGuiKey_Keypad4;
        case 0x57: return ImGuiKey_Keypad5;
        case 0x58: return ImGuiKey_Keypad6;
        case 0x59: return ImGuiKey_Keypad7;
        case 0x5B: return ImGuiKey_Keypad8;
        case 0x5C: return ImGuiKey_Keypad9;
        case 0x45: return ImGuiKey_KeypadAdd;
        case 0x4E: return ImGuiKey_KeypadSubtract;
        case 0x7A: return ImGuiKey_F1;
        case 0x78: return ImGuiKey_F2;
        case 0x63: return ImGuiKey_F3;
        case 0x76: return ImGuiKey_F4;
        case 0x60: return ImGuiKey_F5;
        case 0x61: return ImGuiKey_F6;
        case 0x62: return ImGuiKey_F7;
        case 0x64: return ImGuiKey_F8;
        case 0x74: return ImGuiKey_PageUp;
        case 0x79: return ImGuiKey_PageDown;
        case 0x24:
        case 0x4C: return ImGuiKey_Enter;
        case 0x33: return ImGuiKey_Backspace;
        case 0x7B: return ImGuiKey_LeftArrow;
        case 0x7C: return ImGuiKey_RightArrow;
        case 0x7E: return ImGuiKey_UpArrow;
        case 0x7D: return ImGuiKey_DownArrow;
        case 0x73: return ImGuiKey_Home;
        case 0x77: return ImGuiKey_End;
        case 0x38: return ImGuiKey_LeftShift;
        case 0x3C: return ImGuiKey_RightShift;
        default: return ImGuiKey_None;
    }
}

}  // namespace

bool CocoaGui::set_parent(void *native) {
    destroy();
    NSView *parent = (__bridge NSView *)native;
    if (!parent || ![parent isKindOfClass:[NSView class]]) return false;
    if (!create_gl(parent)) {
        log("gui: OpenGL context creation failed");
        destroy();
        return false;
    }
    ImGuiContext *prev = ImGui::GetCurrentContext();
    ctx_ = ImGui::CreateContext();
    ImGui::SetCurrentContext(ctx_);
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false;
    ImFont *font = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char *>(kPanelFont), static_cast<int>(kPanelFontSize),
                                                  18.0f, &cfg);
    panel_.set_font(font);
    panel_.apply_style();
    ImGui_ImplOpenGL3_Init("#version 150");
    ImGui::SetCurrentContext(prev);
    [NSOpenGLContext clearCurrentContext];
    timer_ = [NSTimer timerWithTimeInterval:1.0 / 60.0 target:view_ selector:@selector(tick:) userInfo:nil repeats:YES];
    [[NSRunLoop mainRunLoop] addTimer:timer_ forMode:NSRunLoopCommonModes];
    log("gui: opened %ux%u on Cocoa", width_, height_);
    return true;
}

bool CocoaGui::create_gl(NSView *parent) {
    NSOpenGLPixelFormatAttribute attrs[] = {NSOpenGLPFAOpenGLProfile,
                                            static_cast<NSOpenGLPixelFormatAttribute>(NSOpenGLProfileVersion3_2Core),
                                            NSOpenGLPFAAccelerated,
                                            NSOpenGLPFADoubleBuffer,
                                            NSOpenGLPFAColorSize,
                                            24,
                                            NSOpenGLPFAAlphaSize,
                                            8,
                                            0};
    NSOpenGLPixelFormat *format = [[NSOpenGLPixelFormat alloc] initWithAttributes:attrs];
    if (!format) return false;
    OpenMMT8PanelView *view = [[OpenMMT8PanelView alloc] initWithFrame:NSMakeRect(0, 0, width_, height_)
                                                           pixelFormat:format
                                                                 owner:this];
    if (!view) return false;
    view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [view setWantsBestResolutionOpenGLSurface:YES];
    [parent addSubview:view];
    view_ = view;
    NSOpenGLContext *gl = view.openGLContext;
    if (!gl) return false;
    GLint swap = 0;  // flushBuffer must not block the host's main thread
    [gl setValues:&swap forParameter:NSOpenGLCPSwapInterval];
    [gl makeCurrentContext];
    return true;
}

void CocoaGui::set_size(uint32_t w, uint32_t h) {
    width_ = w;
    height_ = h;
    if (view_) [view_ setFrame:NSMakeRect(0, 0, w, h)];
}

void CocoaGui::show(bool visible) {
    if (view_) [view_ setHidden:visible ? NO : YES];
    if (!visible) panel_.release_all();
}

void CocoaGui::frame() {
    if (!ctx_ || !view_ || in_modal_ || view_.hidden) return;
    const NSRect bounds = view_.bounds;
    const int W = static_cast<int>(bounds.size.width), H = static_cast<int>(bounds.size.height);
    if (W <= 0 || H <= 0) return;
    ImGuiContext *prev = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(ctx_);
    NSOpenGLContext *gl = view_.openGLContext;
    [gl makeCurrentContext];
    [gl update];
    ImGui_ImplOpenGL3_NewFrame();
    ImGuiIO &io = ImGui::GetIO();
    const NSRect backing = [view_ convertRectToBacking:bounds];
    io.DisplaySize = ImVec2(static_cast<float>(W), static_cast<float>(H));
    io.DisplayFramebufferScale =
        ImVec2(W > 0 ? static_cast<float>(backing.size.width / bounds.size.width) : 1.0f,
               H > 0 ? static_cast<float>(backing.size.height / bounds.size.height) : 1.0f);
    ImGui::NewFrame();
    const bool load_rom = panel_.draw(W, H);
    ImGui::Render();
    glViewport(0, 0, static_cast<GLsizei>(backing.size.width), static_cast<GLsizei>(backing.size.height));
    glClearColor(0.055f, 0.055f, 0.06f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    [gl flushBuffer];
    [NSOpenGLContext clearCurrentContext];
    ImGui::SetCurrentContext(prev);
    if (load_rom) pick_rom();
}

void CocoaGui::mouse_move(float x, float y) {
    if (!ctx_) return;
    ImGuiContext *prev = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(ctx_);
    ImGui::GetIO().AddMousePosEvent(x, y);
    ImGui::SetCurrentContext(prev);
}

void CocoaGui::mouse_button(float x, float y, int button, bool down) {
    if (!ctx_) return;
    ImGuiContext *prev = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(ctx_);
    ImGuiIO &io = ImGui::GetIO();
    io.AddMousePosEvent(x, y);
    io.AddMouseButtonEvent(button, down);
    ImGui::SetCurrentContext(prev);
}

void CocoaGui::mouse_wheel(float dx, float dy) {
    if (!ctx_) return;
    ImGuiContext *prev = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(ctx_);
    ImGui::GetIO().AddMouseWheelEvent(dx / 10.0f, dy / 10.0f);
    ImGui::SetCurrentContext(prev);
}

void CocoaGui::key(unsigned short code, bool down, NSString *chars, bool command) {
    if (!ctx_) return;
    ImGuiContext *prev = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(ctx_);
    ImGuiIO &io = ImGui::GetIO();
    const ImGuiKey key = map_key(code);
    if (key != ImGuiKey_None) io.AddKeyEvent(key, down);
    if (down && chars && !command)
        for (NSUInteger i = 0; i < chars.length; ++i) {
            const unichar c = [chars characterAtIndex:i];
            if (c >= 32 && c < 0xF700) io.AddInputCharacter(static_cast<unsigned>(c));
        }
    ImGui::SetCurrentContext(prev);
}

void CocoaGui::focus_lost() {
    if (!ctx_) return;
    ImGuiContext *prev = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(ctx_);
    panel_.release_keyboard();
    ImGui::SetCurrentContext(prev);
}

void CocoaGui::pick_rom() {
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    panel.title = @"Load ROM";
    panel.canChooseFiles = YES;
    panel.canChooseDirectories = NO;
    panel.allowsMultipleSelection = NO;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    panel.allowedFileTypes = @[@"bin", @"rom", @"hex"];
#pragma clang diagnostic pop
    in_modal_ = true;
    const NSModalResponse response = [panel runModal];
    in_modal_ = false;
    if (response != NSModalResponseOK) return;
    std::string error;
    if (engine_.load_rom_file(panel.URL.path.UTF8String, error)) {
        if (host_.state_changed) host_.state_changed();
    } else {
        panel_.set_status(error.empty() ? "The ROM file did not load." : error);
    }
}

void CocoaGui::destroy() {
    if (timer_) {
        [timer_ invalidate];
        timer_ = nil;
    }
    panel_.release_all();
    if (ctx_) {
        ImGuiContext *prev = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(ctx_);
        if (view_) [view_.openGLContext makeCurrentContext];
        ImGui_ImplOpenGL3_Shutdown();
        ImGui::DestroyContext(ctx_);
        ImGui::SetCurrentContext(prev == ctx_ ? nullptr : prev);
        [NSOpenGLContext clearCurrentContext];
        ctx_ = nullptr;
    }
    if (view_) {
        [view_ detach];
        [view_ removeFromSuperview];
        view_ = nil;
    }
}

bool Gui::supported() { return true; }

std::unique_ptr<Gui> Gui::create(Engine &engine, GuiHost host) {
    return std::unique_ptr<Gui>(new CocoaGui(engine, std::move(host)));
}

}  // namespace mmt8
