// Kinetik — file drops on the plugin window, macOS: the DPF view becomes a dragging destination
// (methods added at run time, so the DPF / pugl sources stay untouched).
#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>
#include "FileDrop.hpp"
#include <map>

namespace {
std::map<void*, kn::FileDropFn>& registry() { static std::map<void*, kn::FileDropFn> m; return m; }
NSDictionary* fileOnly() { return @{NSPasteboardURLReadingFileURLsOnlyKey: @YES}; }

NSDragOperation knDragEntered(id self, SEL, id<NSDraggingInfo> info)
{
    if (registry().count((void*)self) == 0) return NSDragOperationNone;
    return [[info draggingPasteboard] canReadObjectForClasses:@[[NSURL class]] options:fileOnly()] ? NSDragOperationCopy : NSDragOperationNone;
}
BOOL knPrepare(id, SEL, id<NSDraggingInfo>) { return YES; }
BOOL knPerform(id self, SEL, id<NSDraggingInfo> info)
{
    auto it = registry().find((void*)self);
    if (it == registry().end()) return NO;
    NSArray* urls = [[info draggingPasteboard] readObjectsForClasses:@[[NSURL class]] options:fileOnly()];
    if (urls == nil || [urls count] == 0) return NO;
    NSView* v = (NSView*)self;
    const NSPoint p = [v convertPoint:[info draggingLocation] fromView:nil];
    const NSRect b = [v bounds];
    const double fx = b.size.width > 0 ? p.x / b.size.width : 0.5;
    const double fy = b.size.height > 0 ? ([v isFlipped] ? p.y / b.size.height : 1.0 - p.y / b.size.height) : 0.5;
    const char* path = [[(NSURL*)[urls objectAtIndex:0] path] UTF8String];
    if (path != nullptr) it->second(std::string(path), fx, fy);
    return YES;
}
} // namespace

void* kn::fileDropAttach(uintptr_t view, FileDropFn fn)
{
    NSView* v = (NSView*)(void*)view;
    if (v == nil) return nullptr;
    Class c = object_getClass(v);
    class_addMethod(c, @selector(draggingEntered:), (IMP)knDragEntered, "Q@:@");
    class_addMethod(c, @selector(draggingUpdated:), (IMP)knDragEntered, "Q@:@");
    class_addMethod(c, @selector(prepareForDragOperation:), (IMP)knPrepare, "c@:@");
    class_addMethod(c, @selector(performDragOperation:), (IMP)knPerform, "c@:@");
    [v registerForDraggedTypes:@[NSPasteboardTypeFileURL]];
    registry()[(void*)v] = std::move(fn);
    return (void*)v;
}

void kn::fileDropDetach(void* token)
{
    if (token == nullptr) return;
    [(NSView*)token unregisterDraggedTypes];
    registry().erase(token);
}
