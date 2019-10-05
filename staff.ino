#include "FastLED.h"

#define MAX_BRIGHTNESS 50 // TODO: 170
#define COLOR_BLANK 0

#define HIGH_FADE 0.88f
#define MEDIUM_FADE 0.6f
#define LOW_FADE 0.4f
#define NO_FADE 0.2f

#define PIN_KNOCK 34
#define PIN_FSR 35
#define PIN_STRIP_BRANCH 21
#define PIN_STRIP_BASE_0 19
#define PIN_STRIP_BASE_1 18

// Needed for ESP32
#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))

unsigned long current_time = millis();

uint8_t colorPresetsLength = 6;
CRGB colorPresets[6] = {
    {0, 0, 0},   // 0 COLOR_BLANK
    {24, 8, 8},  // 1 COLOR_SOFT_RED
    {8, 24, 8},  // 2 COLOR_SOFT_GREEN
    {8, 8, 24},  // 3 COLOR_SOFT_BLUE
    {24, 8, 24}, // 4 COLOR_SOFT_?
    {24, 24, 8}, // 5 COLOR_SOFT_?
};

struct LEDStrip
{
    uint8_t length;
    uint8_t pin;
    CRGB *state;
};

// #define STRIP_BRANCH_LENGTH 20
#define STRIP_BRANCH_LENGTH 70
CRGB strip_branch_LEDState[STRIP_BRANCH_LENGTH];
constexpr struct LEDStrip strip_branch = {
    .length = STRIP_BRANCH_LENGTH,
    .pin = PIN_STRIP_BRANCH,
    .state = strip_branch_LEDState};

#define STRIP_BASE_0_LENGTH 70
// #define STRIP_BASE_0_LENGTH 20
CRGB strip_base0_LEDState[STRIP_BASE_0_LENGTH];
constexpr struct LEDStrip strip_base0 = {
    .length = STRIP_BASE_0_LENGTH,
    .pin = PIN_STRIP_BASE_0,
    .state = strip_base0_LEDState};

// #define STRIP_BASE_1_LENGTH 20
#define STRIP_BASE_1_LENGTH 70
CRGB strip_base1_LEDState[STRIP_BASE_1_LENGTH];
constexpr struct LEDStrip strip_base1 = {
    .length = STRIP_BASE_1_LENGTH,
    .pin = PIN_STRIP_BASE_1,
    .state = strip_base1_LEDState};

struct AnimationRunnerStatus
{
    bool isAnimatingStrip_base0;
    bool isAnimatingStrip_branch;
};

// Runtime stuff
int loopDelay = 1;
int displayRate = 5; // Every n interpolations
// This will overflow and go back to zero every 50 days if left on continuously, should be fine haha
uint32_t iterationCount = 0;

void setup()
{
    FastLED.addLeds<NEOPIXEL, strip_branch.pin>(strip_branch.state, strip_branch.length);
    fill_solid(strip_branch.state, strip_branch.length, CRGB::Black);

    FastLED.addLeds<NEOPIXEL, strip_base0.pin>(strip_base0.state, strip_base0.length);
    fill_solid(strip_base0.state, strip_base0.length, CRGB::Black);

    FastLED.addLeds<NEOPIXEL, strip_base1.pin>(strip_base1.state, strip_base1.length);
    fill_solid(strip_base1.state, strip_base1.length, CRGB::Black);

    FastLED.setBrightness(MAX_BRIGHTNESS);
    Serial.begin(9600);
    Serial.println("Started");
}

/// MARK - Knock detection
int last3KnockValues[3] = {0, 0, 0};
uint8_t currentKnockValueIndex = 0;
const int threshold = 100;      // threshold value to decide when the detected sound is a knock or not
uint16_t averageKnockValue = 0; // stored for debugging
unsigned long timeOfLastKnock = 0;
#define MIN_TIME_BETWEEN_KNOCKS 2000
uint16_t getAverageKnockValue()
{
    averageKnockValue = (last3KnockValues[0] + last3KnockValues[1] + last3KnockValues[2]) / 3;
    return averageKnockValue;
}

void storeKnockValue(uint16_t knockValue)
{
    currentKnockValueIndex++;
    if (currentKnockValueIndex > 2)
    {
        currentKnockValueIndex = 0;
    }
    last3KnockValues[currentKnockValueIndex] = knockValue;
}

bool checkDidKnock()
{

    // return false; // TODO
    storeKnockValue(analogRead(PIN_KNOCK));
    // Below threshold
    if (getAverageKnockValue() < threshold)
    {
        return false;
    }
    // Serial.println(getAverageKnockValue());
    current_time = millis();

    // Last knock happened too recently
    if (timeOfLastKnock + MIN_TIME_BETWEEN_KNOCKS > current_time)
    {
        return false;
    }
    timeOfLastKnock = current_time;
    return true;
}

/// MARK - Light Strip

// void setPixelRangeToColor(uint8_t startPixel, uint8_t endPixel, struct CRGB color, struct CRGB strip[])
// {
//     for (uint8_t i = startPixel; i <= endPixel; i++)
//     {
//         strip[i] = color;
//     }
// }

void setPixelRangeToColorWithFade(uint8_t startPixel, uint8_t endPixel, struct CRGB color, uint8_t fadeAmount, struct LEDStrip strip)
{
    for (uint8_t i = startPixel; i <= endPixel; i++)
    {
        strip.state[i] = color;
        strip.state[i].fadeToBlackBy(fadeAmount);
    }
}

// FILL_STRIP_STRAND_ANIMATION

struct Animation
{
    uint16_t step;
    uint16_t duration;
    bool isActive;
    CRGB primaryColor;
};

CRGB mainColor = CRGB(0xFFA054);
struct Animation fillAndShimmer_bases = {0, 1000, false, mainColor};
struct Animation fillAndShimmer_branch = {0, 1000, false, mainColor};
struct Animation activeStateAnimation = {0, 5000, false, mainColor};
struct Animation activeStateFSRAnimation = {0, 7000, true, mainColor}; // This is always on but is only used by activeStateAnimation

bool is_animation_finished(struct Animation *animation)
{
    return animation->step >= animation->duration;
}

void start_animation(struct Animation *animation)
{
    animation->step = 0;
    animation->isActive = true;
}

void animateHelper_progressiveSlideIn(float progress, struct CRGB color, struct LEDStrip strip, bool reverse)
{
    uint8_t endPixel = min(progress * strip.length, strip.length - 1);

    for (uint8_t i = 0; i <= endPixel; i++)
    {
        uint8_t scale = 255;
        uint8_t distanceFromEndPixel = endPixel - i;
        if (distanceFromEndPixel <= 3 && endPixel < strip.length - 3)
        {
            scale = 80 / (distanceFromEndPixel + 1); // Ensure we don't divide by zero
            color.fadeToBlackBy(scale);
        }
        if (!reverse)
        {
            strip.state[i] = color;
        }
        else
        {
            strip.state[strip.length - 1 - i] = color;
        }
    }
}

void animateHelper_fadePixel(uint8_t amount, struct LEDStrip strip, uint8_t pixel)
{
    strip.state[pixel].fadeToBlackBy(amount);
}

void animateHelper_fadeBrightness(uint8_t amount, struct LEDStrip strip)
{
    for (uint8_t i = 0; i <= strip.length - 1; i++)
    {
        strip.state[i].fadeToBlackBy(amount);
    }
}

// Helper function that blends one uint8_t toward another by a given amount
void nblendU8TowardU8(uint8_t &cur, const uint8_t target, uint8_t amount)
{
    if (cur == target)
        return;

    if (cur < target)
    {
        uint8_t delta = target - cur;
        delta = scale8_video(delta, amount);
        cur += delta;
    }
    else
    {
        uint8_t delta = cur - target;
        delta = scale8_video(delta, amount);
        cur -= delta;
    }
}

void animateHelper_fadeToColor(struct CRGB target, uint8_t amount, struct LEDStrip strip)
{
    for (uint8_t i = 0; i <= strip.length - 1; i++)
    {
        nblendU8TowardU8(strip.state[i].red, target.red, amount);
        nblendU8TowardU8(strip.state[i].green, target.green, amount);
        nblendU8TowardU8(strip.state[i].blue, target.blue, amount);
    }
}

#define BETWEEN(value, min, max) (value <= max && value >= min)

void animateStep_SLIDE_IN_AND_SHIMMER(struct Animation *animation, struct LEDStrip strip, bool reverse)
{
    float progress = (float)animation->step / (float)animation->duration;
    // progress = easing_easeOutQuad(progress);
    if (BETWEEN(progress, 0, 1))
    {
        CRGB color = animation->primaryColor;
        color.fadeToBlackBy(255.f * HIGH_FADE);
        animateHelper_progressiveSlideIn(progress, color, strip, reverse);
    }
}

// void animateStep_SLIDE_IN_SLIDE_OUT(struct Animation *animation, struct LEDStrip strip)
// {
//     float progress = easing_easeOutQuad((float)animation->step / (float)animation->duration) * 2;
//     CRGB color = animation->primaryColor;
//     if (progress > 1)
//     {
//         progress -= 1;
//         color = CRGB::Black;
//     }
//     animateHelper_progressiveSlideIn(progress, color, strip);
//     animation->step++;
// }

float averageRawForce = 0;
uint8_t averageFade = 255;
bool TRAILING_FSR = true;
const float forceFloor = 0.65;
void refreshFSRForce()
{
    averageRawForce = (averageRawForce + analogRead(PIN_FSR) / 1000.f) / 2.f;
}

float getFSRForce()
{
    // averageRawForce = (averageRawForce + analogRead(A3) / 1000.f) / 2.f;
    if (averageRawForce < forceFloor)
    {
        return 0;
    }
    float normalizedForce = max(averageRawForce - forceFloor, 0) * 3.2;
    return min(normalizedForce, 1);
}

void animateFromFSRInput(float normalizedForce, struct LEDStrip strip, struct CRGB color, uint8_t currentPixel)
{
    // This should be named better
    uint16_t x = 255 * normalizedForce;
    uint8_t sub = (x * x) / 255;
    uint8_t fade = 255 - sub;
    averageFade = ((averageFade * 10) + fade) / 11;
    fade = averageFade;
    if (!TRAILING_FSR)
    {
        // Standard static animation
        setPixelRangeToColorWithFade(0, strip.length - 1, color, fade, strip);
        return;
    }
    // // Run TRAILING_FSR animation
    // if (currentPixelTrailingFSR > strip.length - 1)
    // {
    //     currentPixelTrailingFSR = 0;
    // }
    // else if (iterationCount % 27 == 0) // Must be odd numbers lol
    // {
    //     currentPixelTrailingFSR++;
    //     animateHelper_fadeBrightness(1, strip);
    // }
    strip.state[currentPixel] = color;
    strip.state[currentPixel].fadeToBlackBy(fade);

    // uint8_t stripEndPixel = strip.length - 1;
    // uint8_t otherSide = (currentPixelTrailingFSR + (stripEndPixel / 2)) % stripEndPixel;
    // strip.state[otherSide] = color;
    // strip.state[otherSide].fadeToBlackBy(fade);
}

bool canReadKnockInput()
{
    // return false;
    return iterationCount % 2 == 0;
}

bool canReadFSRInput()
{
    return iterationCount % 2 != 0;
}

// Returns true if timed animation runs
struct AnimationRunnerStatus runTimedAnimations(struct AnimationRunnerStatus status)
{
    if (fillAndShimmer_bases.isActive)
    {
        // if(iterationCount % 200 == 0) {
        //         Serial.println("fillAndShimmer_bases");
        //     }

        if (is_animation_finished(&fillAndShimmer_bases))
        {
            fillAndShimmer_bases.isActive = false;
            start_animation(&fillAndShimmer_branch);
            return status;
        }
        status.isAnimatingStrip_base0 = true;
        // TODO
        animateStep_SLIDE_IN_AND_SHIMMER(&fillAndShimmer_bases, strip_base0, true);
        animateStep_SLIDE_IN_AND_SHIMMER(&fillAndShimmer_bases, strip_base1, true);
        fillAndShimmer_bases.step++;

        return status;
    }

    if (fillAndShimmer_branch.isActive)
    {
        // if(iterationCount % 200 == 0) {
        //         Serial.println("fillAndShimmer_branch");
        //     }
        if (is_animation_finished(&fillAndShimmer_branch))
        {
            fillAndShimmer_branch.isActive = false;
            start_animation(&activeStateAnimation);
            return status;
        }
        status.isAnimatingStrip_base0 = true;
        animateStep_SLIDE_IN_AND_SHIMMER(&fillAndShimmer_branch, strip_branch, false);
        fillAndShimmer_branch.step++;

        return status;
    }

    return status;
}

struct AnimationRunnerStatus runDynamicAnimations(struct AnimationRunnerStatus status)
{
    // float averageNormalizedFSRForce = 0;
    // if (canReadFSRInput() && !status.isAnimatingStrip_branch)
    // {
    //     refreshFSRForce();
    //     averageNormalizedFSRForce = getFSRForce();
    // }
    // if (averageNormalizedFSRForce != 0)
    // {
    //     // Serial.println(averageNormalizedFSRForce);
    //     animateFromFSRInput(averageNormalizedFSRForce, strip_branch, CRGB::Plum);
    //     animateFromFSRInput(averageNormalizedFSRForce, strip_base0, CRGB::Plum);
    //     animateFromFSRInput(averageNormalizedFSRForce, strip_base1, CRGB::Plum);
    //     status.isAnimatingStrip_branch = true;
    // }
    return status;
}

bool atLastStep = false;
bool hasGoneThroughFullActiveStateAnimation = false;
uint8_t lastStepTrailPixel = 0;
struct AnimationRunnerStatus runActiveStateAnimation(struct AnimationRunnerStatus status)
{
    if (activeStateAnimation.isActive)
    {
        // float floor = DEFAULT_FLOOR;
        // if (activeStateAnimation.step <= 255 && !hasGoneThroughFullActiveStateAnimation)
        // {
        //     floor = 1 - ((float)activeStateAnimation.step / 255.f);
        //     floor = max(floor, DEFAULT_FLOOR);
        // }
        // uint16_t fade = 255 - (255 * floor + (quadwave8(millis()) * (1.f - floor)));

        if (canReadFSRInput())
        {
            refreshFSRForce();
        }
        // Serial.println(getFSRForce());
        bool isIncrementing = true;
        if (getFSRForce() > 0.5)
        {
            if (activeStateFSRAnimation.step < activeStateFSRAnimation.duration)
            {
                activeStateFSRAnimation.step += 1;
                isIncrementing = true;
            }
        }
        else
        {
            if (activeStateFSRAnimation.step > 1)
            {
                activeStateFSRAnimation.step -= 1;
                isIncrementing = false;
            }
        }

        float progress = (float)activeStateFSRAnimation.step / (float)activeStateFSRAnimation.duration;
        progress *= 4.f;
        // Serial.println(progress);

        CRGB color = activeStateAnimation.primaryColor;
        bool atLastStep = BETWEEN(progress, 3, 5);
        uint8_t fade;
        if (BETWEEN(progress, 0, 1))
        {
            fade = 255.f * MEDIUM_FADE;
        }
        else if (BETWEEN(progress, 1, 2))
        {
            progress -= 1;
            fade = 255.f * LOW_FADE;
        }
        else if (BETWEEN(progress, 2, 3))
        {
            progress -= 2;
            fade = 255.f * NO_FADE;
        }
        else if (BETWEEN(progress, 3, 5))
        { // Did 5 just in case of floating precision errors
            progress -= 3;
            fade = 255.f * NO_FADE;
            // if (iterationCount % 200 == 0)
            // {
            //     Serial.println("NO_FADE");
            // }
        }
        // progress is now between 0 and 1

        if (is_animation_finished(&activeStateAnimation))
        {
            hasGoneThroughFullActiveStateAnimation = true;
            start_animation(&activeStateAnimation);
        }

        color.fadeToBlackBy(fade);

        if (atLastStep)
        {

            // float sin = (float)sin8(iterationCount) / 255.f;
            // float cos = (float)cos8(iterationCount) / 255.f;
            uint8_t pixel;
            if (lastStepTrailPixel < STRIP_BASE_0_LENGTH)
            {
                pixel = STRIP_BASE_0_LENGTH - 1 - lastStepTrailPixel;
                animateFromFSRInput(1.f, strip_base0, CRGB::DarkOliveGreen, pixel);
                animateFromFSRInput(1.f, strip_base1, CRGB::DarkOliveGreen, pixel);
            }
            else
            {
                pixel = lastStepTrailPixel - STRIP_BASE_0_LENGTH - 1;
                animateFromFSRInput(1.f, strip_branch, CRGB::DarkOliveGreen, pixel);
            }

            if (lastStepTrailPixel > (STRIP_BASE_0_LENGTH * 2) - 1)
            {
                lastStepTrailPixel = 0;
            }
            else
            {
                if (iterationCount % 17 == 0)
                {
                    lastStepTrailPixel++;
                }
            }
            if (iterationCount % 4 == 0)
            {
                animateHelper_fadeToColor(color, 2, strip_base0);
                animateHelper_fadeToColor(color, 2, strip_base1);
                animateHelper_fadeToColor(color, 2, strip_branch);
            }
        }
        else
        {
            // Base animation state
            if (BETWEEN(progress, 0.f, 0.01f))
            {
                CRGB base0Color = color;
                CRGB base1Color = color;
                CRGB branchColor = color;
                current_time = millis();
                uint8_t timeBasedSinWave = sin8(current_time) / 4;
                uint8_t timeBasedCosWave = cos8(current_time) / 4;
                base0Color.fadeLightBy(timeBasedSinWave);
                base1Color.fadeLightBy(timeBasedSinWave);
                branchColor.fadeLightBy(timeBasedCosWave);
                // animateHelper_fadeToColor(base0Color, 2, strip_base0);
                // animateHelper_fadeToColor(base1Color, 2, strip_base1);
                // animateHelper_fadeToColor(branchColor, 2, strip_branch);
                fill_solid(strip_base0.state, strip_base0.length, base0Color);
                fill_solid(strip_base1.state, strip_base1.length, base1Color);
                fill_solid(strip_branch.state, strip_branch.length, branchColor);
            }
            else if (BETWEEN(progress, 0.f, 0.5f))
            {
                animateHelper_progressiveSlideIn(progress * 2.f, color, strip_base0, true);
                animateHelper_progressiveSlideIn(progress * 2.f, color, strip_base1, true);
            }
            else if (BETWEEN(progress, 0.5f, 1.f))
            {
                progress -= 0.5f;
                animateHelper_progressiveSlideIn(progress * 2.f, color, strip_branch, false);
            }
        }

        status.isAnimatingStrip_base0 = true;
        status.isAnimatingStrip_branch = true;
        activeStateAnimation.step++;
        return status;
    }
    return status;
}

struct AnimationRunnerStatus runFadeAnimations(struct AnimationRunnerStatus status)
{
    // if (iterationCount % 2 != 0)
    // {
    //     return status;
    // }

    // if (!status.isAnimatingStrip_base0)
    // {
    //     animateHelper_fadeBrightness(1, strip_base0);
    //     status.isAnimatingStrip_base0 = true;
    // }

    // animateHelper_fadeBrightness(1, strip_base1);

    // if (!status.isAnimatingStrip_branch)
    // {
    //     animateHelper_fadeBrightness(1, strip_branch);
    //     status.isAnimatingStrip_branch = true;
    // }
    // return status;
}

AnimationRunnerStatus animationFrameStatus;
void runAnimationFrame()
{
    animationFrameStatus = {.isAnimatingStrip_base0 = false, .isAnimatingStrip_branch = false};
    animationFrameStatus = runTimedAnimations(animationFrameStatus);
    // animationFrameStatus = runDynamicAnimations(animationFrameStatus);
    animationFrameStatus = runActiveStateAnimation(animationFrameStatus);
    // runFadeAnimations(animationFrameStatus);
}

// unsigned long startTime = 0;
// unsigned long endTime = 0;

void loop()
{
    if (!activeStateAnimation.isActive && canReadKnockInput() && checkDidKnock()) // Light strip animations seem to make knock sensor readings higher (???) - may be due to current draw
    {
        start_animation(&fillAndShimmer_bases);
    }

    runAnimationFrame();

    if (iterationCount % 4 == 0)
    {

        FastLED.show();
        // endTime = millis();
        // Serial.println("v delta v");
        // Serial.println(millis() - endTime);
    }

    // delayMicroseconds(1000);
    delay(1);
    iterationCount++;
}