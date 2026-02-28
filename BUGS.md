# Known Bugs

## System-wide stutter triggered by scroll wheel + Present() calls

### Symptoms
- After rapid scrolling (zoom or Ctrl+exposure), mouse movement stutters at ~10fps for 5-10 seconds
- Affects the ENTIRE system: other apps, background video, mouse cursor — not just EXRay
- Intermittent: ~25-50% reproducible
- Resolves on its own after 5-10 seconds
- Triggered equally by zoom-in and zoom-out
- Triggered by scroll wheel specifically, not by keyboard/mouse/pan input
- Hardware: Logitech MagSpeed (high-inertia free-spin) scroll wheel, 175Hz display

### Root cause (confirmed)
- It is the `Present()` calls during scroll that trigger it — NOT the scroll message volume
- **Diagnostic proof**: Commenting out `m_needsRedraw = true` in the scroll handler (so scroll updates internal state but never renders) completely eliminates the stutter
- The issue is in the GPU driver or DWM compositor, not in our code

### What we tried

| Approach | Result |
|----------|--------|
| Throttle to 60fps during scroll | Still stutters |
| Present(0, 0) (no vsync) during scroll | Still stutters |
| Present(1, 0) (vsync) during scroll | Still stutters |
| DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT + SetMaximumFrameLatency(1) | Fixed screen blanking and cursor rendering issues, but scroll stutter persists |
| Properly waiting on frame latency waitable handle before each render | Still stutters |
| Throttle to 30fps during scroll | No stutter, but bad UX |
| Throttle to 5fps during scroll | No stutter, bad UX |
| Complete render debounce (0 renders during scroll, 1 render when scroll stops) | No stutter, worst UX |
| Disabling rendering entirely during scroll (diagnostic) | No stutter — confirms Present() is the trigger |

### Key observations
- Any sustained `Present()` calls during scroll input triggers the issue, regardless of vsync mode or frame rate (even 60fps triggers it)
- 30fps was the highest rate tested that avoided the stutter on this hardware, but that's hardware-specific and not a real fix
- Before adding `DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT`, the issue also caused brief screen blackouts (~30-100ms) and the mouse cursor lost its black outline (rendered as hardware cursor without antialiasing). These symptoms suggest DXGI independent flip / MPO mode transitions. The waitable object flag fixed those visual artifacts but not the underlying stutter.
- The stutter is NOT present in other apps rendering at the same refresh rate during scroll — so something about our specific DXGI setup is different

### Theories to investigate next

1. **Child window swap chain**: We create the swap chain on a WS_CHILD render area window, not the main window. This is unusual — most apps use the main window or DirectComposition. The DWM/driver may handle child window swap chains differently, possibly falling back to a less optimal composition path that interacts badly with rapid presents during scroll input.

2. **DirectComposition swap chain**: Try `CreateSwapChainForComposition` + `IDCompositionDevice` instead of `CreateSwapChainForHwnd`. This is what modern apps (Chrome, Edge, etc.) use and gives the compositor explicit control over presentation, potentially avoiding the driver issue entirely.

3. **Move swap chain to main window**: Instead of a child window, target the main window and use D3D11 viewport/scissor to render only in the area below the tab bar and above the status bar. Eliminates the child window variable.

4. **DXGI_SCALING_STRETCH vs DXGI_SCALING_NONE**: We use DXGI_SCALING_NONE (for clean resize behavior). Try reverting to SCALING_STRETCH to see if this flag contributes to the issue. Would need an alternative solution for resize stretching.

5. **Test on different GPU/driver**: The issue may be specific to the current GPU driver. Testing on different hardware would confirm whether it's a universal issue or driver-specific.

6. **GPU driver version**: Check if updating/rolling back the GPU driver changes behavior.

### Related changes made during investigation (may want to keep)
- `DXGI_SCALING_NONE` on swap chain — prevents DWM from stretching old frame during resize
- `DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT` + `SetMaximumFrameLatency(1)` — prevents independent flip mode transitions (screen blanking, cursor artifacts)
- Render in `OnResize()` with `Present(0, 0)` — provides live redraw during window resize drag
- `EndFrame(bool vsync)` parameter — useful for resize renders
