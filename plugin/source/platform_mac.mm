#include "filedialog.h"

#import <Cocoa/Cocoa.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

namespace sq {

std::string chooseFile(void* parentWindow, bool save, const std::string& title, const std::string& defaultName) {
    (void)parentWindow; // modal panels; sheets would need a run loop the host may not give us
    @autoreleasepool {
        NSArray<UTType*>* types = @[ UTTypePlainText ];
        NSSavePanel* panel;
        if (save) {
            NSSavePanel* p = [NSSavePanel savePanel];
            p.nameFieldStringValue = [NSString stringWithUTF8String:defaultName.c_str()];
            panel = p;
        } else {
            NSOpenPanel* p = [NSOpenPanel openPanel];
            p.canChooseDirectories = NO;
            p.allowsMultipleSelection = NO;
            panel = p;
        }
        panel.allowedContentTypes = types;
        panel.message = [NSString stringWithUTF8String:title.c_str()];
        if ([panel runModal] != NSModalResponseOK || !panel.URL) return {};
        return panel.URL.path.UTF8String;
    }
}

void embedChildView(void* parent, void* child, int width, int height) {
    NSView* parentView = (__bridge NSView*)parent;
    NSView* view = (__bridge NSView*)child;
    if (!parentView || !view) return;
    view.frame = NSMakeRect(0, 0, width, height);
    view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [parentView addSubview:view];
}

void resizeChildView(void* child, int width, int height) {
    NSView* view = (__bridge NSView*)child;
    if (view) [view setFrameSize:NSMakeSize(width, height)];
}

void removeChildView(void* child) {
    NSView* view = (__bridge NSView*)child;
    if (view) [view removeFromSuperview];
}

} // namespace sq
