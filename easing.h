// EASING_FUNCTIONS:
// no easing, no acceleration
float easing_linear(float t) { return t; }
// accelerating from zero velocity
float easing_easeInQuad(float t) { return t * t; }
// decelerating to zero velocity
float easing_easeOutQuad(float t) { return t * (2.0f - t); }
// acceleration until halfway, then deceleration
float easing_easeInOutQuad(float t) { return t < .5 ? 2 * t * t : -1 + (4 - 2 * t) * t; }
// accelerating from zero velocity
float easing_easeInCubic(float t) { return t * t * t; }
// decelerating to zero velocity
float easing_easeOutCubic(float t) { return (--t) * t * t + 1.f; }
// acceleration until halfway, then deceleration
float easing_easeInOutCubic(float t) { return t < .5 ? 4 * t * t * t : (t - 1) * (2 * t - 2) * (2 * t - 2) + 1; }
// accelerating from zero velocity
float easing_easeInQuart(float t) { return t * t * t * t; }
// decelerating to zero velocity
float easing_easeOutQuart(float t) { return 1 - (--t) * t * t * t; }
// acceleration until halfway, then deceleration
float easing_easeInOutQuart(float t) { return t < .5 ? 8 * t * t * t * t : 1 - 8 * (--t) * t * t * t; }
// accelerating from zero velocity
float easing_easeInQuint(float t) { return t * t * t * t * t; }
// decelerating to zero velocity
float easing_easeOutQuint(float t) { return 1 + (--t) * t * t * t * t; }
// acceleration until halfway, then deceleration
float easing_easeInOutQuint(float t) { return t < .5 ? 16 * t * t * t * t * t : 1 + 16 * (--t) * t * t * t * t; }