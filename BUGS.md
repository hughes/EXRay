# Known Bugs

(none currently)

---

# Resolved Bugs

## System-wide stutter triggered by scroll wheel + Present() calls

**Status: FIXED** — DirectComposition swap chain with effect group opacity

### Symptoms
- After rapid scrolling (zoom or Ctrl+exposure), mouse movement stuttered at ~10fps for 5-10 seconds
- Affected the ENTIRE system: other apps, background video, mouse cursor — not just EXRay
- Intermittent: ~25-50% reproducible
- Triggered by scroll wheel specifically, not by keyboard/mouse/pan input
- Hardware: Logitech MagSpeed (high-inertia free-spin) scroll wheel, 175Hz display

### Root cause
The DWM was promoting the swap chain to **independent flip mode** (handing it directly to the display hardware). Rapid `Present()` calls during scroll caused the DWM to repeatedly transition between composed and independent flip modes. The **mode transition** itself caused the system-wide stutter — not the rendering load.

**Key evidence**: Running any other windowed app with an active swap chain (even at 60fps) eliminated the stutter, because the DWM was forced to stay in composed mode (can't independent-flip when multiple windows are presenting).

### Fix
Switched from `CreateSwapChainForHwnd` to `CreateSwapChainForComposition` (DirectComposition) and attached an `IDCompositionEffectGroup` with 254/255 opacity to the visual. The effect group forces the DWM to always composite the visual, preventing independent flip promotion and the problematic mode transitions.

Neither DirectComposition alone nor `IDCompositionVisual3::SetOpacity` alone was sufficient — the DWM optimized those away and still promoted to independent flip. Only an explicit `IDCompositionEffectGroup` with a non-identity effect prevented the promotion. An identity 3D transform (`IDCompositionMatrixTransform3D`) was also optimized away.

The 254/255 opacity introduces a ~0.4% luminance reduction — imperceptible to the eye but measurable with a colorimeter. This is an acceptable tradeoff; no zero-impact alternative exists.

### What we tried (for reference)

| Approach | Result |
|----------|--------|
| Throttle to 60fps during scroll | Still stutters |
| Present(0, 0) (no vsync) during scroll | Still stutters |
| Present(1, 0) (vsync) during scroll | Still stutters |
| DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT | Fixed screen blanking and cursor artifacts, stutter persists |
| Throttle to 30fps during scroll | No stutter, but bad UX |
| DirectComposition swap chain (no effect group) | Still stutters |
| IDCompositionVisual3::SetOpacity(254/255) | Still stutters |
| IDCompositionEffectGroup + identity 3D transform | Still stutters (DWM optimizes identity away) |
| **DirectComposition + IDCompositionEffectGroup opacity** | **Fixed** |
