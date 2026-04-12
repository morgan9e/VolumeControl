// VolumeControl
//
// Provides software volume control for audio devices that lack hardware volume
// (e.g. HDMI outputs). Also works as a universal volume control for all devices.

#import <Cocoa/Cocoa.h>
#import <AVFoundation/AVCaptureDevice.h>

#import "VCAudioDeviceManager.h"
#import "VCTermination.h"

@protocol OSDUIHelperProtocol
- (void)showImage:(long long)image onDisplayID:(unsigned int)displayID
         priority:(unsigned int)priority msecUntilFade:(unsigned int)fade
    filledChiclets:(unsigned int)filled totalChiclets:(unsigned int)total
           locked:(BOOL)locked;
@end

static const long long kOSDImageVolume = 3;

static void ShowVolumeOSD(float volume) {
    static NSXPCConnection* conn = nil;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        conn = [[NSXPCConnection alloc] initWithMachServiceName:@"com.apple.OSDUIHelper"
                                                        options:0];
        conn.remoteObjectInterface = [NSXPCInterface interfaceWithProtocol:@protocol(OSDUIHelperProtocol)];
        [conn resume];
    });

    unsigned int filled = (unsigned int)(volume * 16 + 0.5f);
    unsigned int total = 16;

    id<OSDUIHelperProtocol> proxy = [conn remoteObjectProxy];
    [proxy showImage:kOSDImageVolume
         onDisplayID:CGMainDisplayID()
            priority:0x1f4
       msecUntilFade:1000
      filledChiclets:filled
       totalChiclets:total
              locked:NO];
}


// Minimal app delegate — handles termination cleanup.
@interface VCAppDelegate : NSObject <NSApplicationDelegate>
@property (nonatomic) VCAudioDeviceManager* audioDevices;
@end

@implementation VCAppDelegate
- (void) applicationWillTerminate:(NSNotification*)note {
    #pragma unused (note)
    if ([self.audioDevices isVirtualDeviceActive]) {
        [self.audioDevices unsetVCDeviceAsOSDefault];
    }
}
@end


// Menu bar controller — speaker icon with scroll-to-adjust volume.
@interface VCMenuBar : NSObject <NSMenuDelegate>
@property (nonatomic) NSStatusItem* statusItem;
@property (nonatomic) VCAudioDeviceManager* audioDevices;
@property (nonatomic) id scrollMonitor;
@property (nonatomic) NSSlider* volumeSlider;
@end

@implementation VCMenuBar

- (void) sliderChanged:(NSSlider*)sender {
    [self.audioDevices setVolume:sender.floatValue];
    [self updateIcon];
}

- (void) menuWillOpen:(NSMenu*)menu {
    self.volumeSlider.floatValue = [self.audioDevices volume];
}

- (void) setupWithAudioDevices:(VCAudioDeviceManager*)devices {
    self.audioDevices = devices;
    self.statusItem = [[NSStatusBar systemStatusBar] statusItemWithLength:NSVariableStatusItemLength];

    [self updateIcon];

    NSMenu* menu = [[NSMenu alloc] init];
    menu.delegate = self;

    NSMenuItem* label = [[NSMenuItem alloc] initWithTitle:@"VolumeControl" action:nil keyEquivalent:@""];
    [label setEnabled:NO];
    [menu addItem:label];
    [menu addItem:[NSMenuItem separatorItem]];

    // Slider menu item.
    NSSlider* slider = [NSSlider sliderWithValue:[self.audioDevices volume]
                                        minValue:0.0
                                        maxValue:1.0
                                          target:self
                                          action:@selector(sliderChanged:)];
    slider.controlSize = NSControlSizeSmall;

    NSView* sliderView = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 200, 28)];
    slider.frame = NSMakeRect(14, 4, 172, 20);
    [sliderView addSubview:slider];
    self.volumeSlider = slider;

    NSMenuItem* sliderItem = [[NSMenuItem alloc] init];
    sliderItem.view = sliderView;
    [menu addItem:sliderItem];

    [menu addItem:[NSMenuItem separatorItem]];
    [menu addItemWithTitle:@"Quit" action:@selector(terminate:) keyEquivalent:@"q"];

    self.statusItem.menu = menu;

    // Scroll on status bar icon to adjust volume.
    self.scrollMonitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskScrollWheel
                                                               handler:^NSEvent*(NSEvent* event) {
        // Only respond if the mouse is over our status item.
        NSRect frame = self.statusItem.button.window.frame;
        NSPoint mouse = [NSEvent mouseLocation];
        if (!NSPointInRect(mouse, frame)) {
            return event;
        }

        float delta = event.scrollingDeltaY;
        if (event.hasPreciseScrollingDeltas) {
            delta *= 0.002f;  // Trackpad: fine-grained
        } else {
            delta *= 0.05f;   // Mouse wheel: coarser steps
        }

        float vol = [self.audioDevices volume] + delta;
        [self.audioDevices setVolume:vol];
        [self updateIcon];
        ShowVolumeOSD([self.audioDevices volume]);

        return nil;  // Consume the event.
    }];
}

- (void) dealloc {
    if (self.scrollMonitor) {
        [NSEvent removeMonitor:self.scrollMonitor];
    }
}

- (void) updateIcon {
    float vol = [self.audioDevices volume];
    if (self.volumeSlider) {
        self.volumeSlider.floatValue = vol;
    }
    BOOL muted = [self.audioDevices isMuted];

    NSString* symbolName;
    if (muted || vol < 0.01f) {
        symbolName = @"speaker.fill";
    } else if (vol < 0.33f) {
        symbolName = @"speaker.wave.1.fill";
    } else if (vol < 0.66f) {
        symbolName = @"speaker.wave.2.fill";
    } else {
        symbolName = @"speaker.wave.3.fill";
    }

    NSImage* icon = [NSImage imageWithSystemSymbolName:symbolName
                             accessibilityDescription:@"VolumeControl"];
    [icon setTemplate:YES];
    self.statusItem.button.image = icon;
}

@end


int main(int argc, const char* argv[]) {
    #pragma unused (argc, argv)

    @autoreleasepool {
        NSApplication* app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyAccessory];

        NSLog(@"VolumeControl: Starting...");

        VCAudioDeviceManager* audioDevices = [VCAudioDeviceManager new];

        if (!audioDevices) {
            NSLog(@"VolumeControl: Could not find the virtual audio device driver.");
            return 1;
        }

        VCTermination::SetUpTerminationCleanUp(audioDevices);

        // App delegate for clean shutdown.
        VCAppDelegate* delegate = [[VCAppDelegate alloc] init];
        delegate.audioDevices = audioDevices;
        [app setDelegate:delegate];

        // Menu bar icon with scroll volume control.
        VCMenuBar* menuBar = [[VCMenuBar alloc] init];
        [menuBar setupWithAudioDevices:audioDevices];

        // Update icon when volume changes externally (keyboard keys, system slider).
        audioDevices.onVolumeChanged = ^{
            [menuBar updateIcon];
        };

        // Request microphone permission. Block until granted.
        if (@available(macOS 10.14, *)) {
            dispatch_semaphore_t sem = dispatch_semaphore_create(0);
            __block BOOL granted = NO;

            [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio
                                     completionHandler:^(BOOL g) {
                granted = g;
                dispatch_semaphore_signal(sem);
            }];

            dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);

            if (!granted) {
                NSLog(@"VolumeControl: Microphone permission denied.");
                return 1;
            }
        }

        // Scan devices and activate.
        [audioDevices evaluateAndActivate];
        [menuBar updateIcon];

        NSLog(@"VolumeControl: Running.");

        [app run];
    }

    return 0;
}
