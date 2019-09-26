#include "FastLED.h"

#define MAX_BRIGHTNESS 20
#define COLOR_BLANK 0

#define BASE_FADE 0.94f

#define PIN_KNOCK 33
#define PIN_FSR 32
#define PIN_STRIP_BRANCH 18
#define PIN_STRIP_BASE_0 19

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
#define STRIP_BRANCH_LENGTH 1
CRGB strip_branch_LEDState[STRIP_BRANCH_LENGTH];
constexpr struct LEDStrip strip_branch = {
    .length = STRIP_BRANCH_LENGTH,
    .pin = PIN_STRIP_BRANCH,
    .state = strip_branch_LEDState};

// #define STRIP_BASE_0_LENGTH 60
#define STRIP_BASE_0_LENGTH 5
CRGB strip_base0_LEDState[STRIP_BASE_0_LENGTH];
constexpr struct LEDStrip strip_base0 = {
    .length = STRIP_BASE_0_LENGTH,
    .pin = PIN_STRIP_BASE_0,
    .state = strip_base0_LEDState};

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
    fill_solid(strip_branch.state, strip_branch.length, CRGB::Blue);

    FastLED.addLeds<NEOPIXEL, strip_base0.pin>(strip_base0.state, strip_base0.length);
    fill_solid(strip_base0.state, strip_base0.length, CRGB::Black);

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
    Serial.println(analogRead(PIN_KNOCK));
    return false; // TODO
    storeKnockValue(analogRead(PIN_KNOCK));
    // Below threshold
    if (getAverageKnockValue() < threshold)
    {
        return false;
    }
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

struct Animation fillAndShimmer_bases = {0, 700, false, CRGB::ForestGreen};
struct Animation activeStateAnimation = {0, 5000, false, CRGB::ForestGreen};

bool is_animation_finished(struct Animation *animation)
{
    return animation->step >= animation->duration;
}

void start_animation(struct Animation *animation)
{
    animation->step = 0;
    animation->isActive = true;
}

void animateHelper_progressiveSlideIn(float progress, struct CRGB color, struct LEDStrip strip)
{
    uint8_t endPixel = min(progress * strip.length, strip.length - 1);
    for (uint8_t i = 0; i <= endPixel; i++)
    {
        uint8_t scale = 255;
        uint8_t distanceFromEndPixel = endPixel - i;
        if (distanceFromEndPixel <= 3 && endPixel < strip.length - 3)
        {
            scale = (80 / distanceFromEndPixel);
            color.fadeToBlackBy(scale);
        }

        strip.state[i] = color;
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

#define BETWEEN(value, min, max) (value <= max && value >= min)

void animateStep_SLIDE_IN_AND_SHIMMER(struct Animation *animation, struct LEDStrip strip)
{
    float progress = (float)animation->step / (float)animation->duration;
    progress = easing_easeOutQuad(progress);
    if (BETWEEN(progress, 0, 1))
    {
        CRGB color = animation->primaryColor;
        color.fadeToBlackBy(255.f * BASE_FADE);
        animateHelper_progressiveSlideIn(progress, color, strip);
    }
    animation->step++;
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
uint8_t currentPixelTrailingFSR = 0;
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

void animateFromFSRInput(float normalizedForce, struct LEDStrip strip)
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
        setPixelRangeToColorWithFade(0, strip.length - 1, CRGB::Plum, fade, strip);
        return;
    }
    // Run TRAILING_FSR animation
    if (currentPixelTrailingFSR > strip.length - 1)
    {
        currentPixelTrailingFSR = 0;
    }
    else if (iterationCount % 17 == 0) // Must be odd numbers lol
    {
        currentPixelTrailingFSR++;
        animateHelper_fadeBrightness(1, strip);
    }
    strip.state[currentPixelTrailingFSR] = CRGB::Plum;
    strip.state[currentPixelTrailingFSR].fadeToBlackBy(fade);

    uint8_t stripEndPixel = strip.length - 1;
    uint8_t otherSide = (currentPixelTrailingFSR + (stripEndPixel / 2)) % stripEndPixel;
    strip.state[otherSide] = CRGB::Plum;
    strip.state[otherSide].fadeToBlackBy(fade);
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
        if (is_animation_finished(&fillAndShimmer_bases))
        {
            fillAndShimmer_bases.isActive = false;
            start_animation(&activeStateAnimation);
            return status;
        }
        status.isAnimatingStrip_base0 = true;
        animateStep_SLIDE_IN_AND_SHIMMER(&fillAndShimmer_bases, strip_base0);
        return status;
    }

    return status;
}

struct AnimationRunnerStatus runDynamicAnimations(struct AnimationRunnerStatus status)
{
    float averageNormalizedFSRForce = 0;
    if (canReadFSRInput() && !status.isAnimatingStrip_branch)
    {
        refreshFSRForce();
        averageNormalizedFSRForce = getFSRForce();
    }
    if (averageNormalizedFSRForce != 0)
    {
        animateFromFSRInput(averageNormalizedFSRForce, strip_branch);
        status.isAnimatingStrip_branch = true;
    }
    return status;
}

bool hasGoneThroughFullActiveStateAnimation = false;
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
        uint8_t fade = 255 * BASE_FADE;
        fade -= getFSRForce() * BASE_FADE * 255;

        CRGB color = activeStateAnimation.primaryColor;
        if (is_animation_finished(&activeStateAnimation))
        {
            hasGoneThroughFullActiveStateAnimation = true;
            start_animation(&activeStateAnimation);
        }

        color.fadeToBlackBy(fade);
        fill_solid(strip_branch.state, strip_branch.length, color);
        fill_solid(strip_base0.state, strip_branch.length, color);
        status.isAnimatingStrip_base0 = true;
        status.isAnimatingStrip_branch = true;
        activeStateAnimation.step++;
        return status;
    }
    return status;
}

struct AnimationRunnerStatus runFadeAnimations(struct AnimationRunnerStatus status)
{
    if (iterationCount % 2 != 0)
    {
        return status;
    }

    if (!status.isAnimatingStrip_base0)
    {
        animateHelper_fadeBrightness(1, strip_base0);
        status.isAnimatingStrip_base0 = true;
    }
    if (!status.isAnimatingStrip_branch)
    {
        animateHelper_fadeBrightness(1, strip_branch);
        status.isAnimatingStrip_branch = true;
    }
    return status;
}

AnimationRunnerStatus animationFrameStatus;
void runAnimationFrame()
{
    animationFrameStatus = {.isAnimatingStrip_base0 = false, .isAnimatingStrip_branch = false};
    animationFrameStatus = runTimedAnimations(animationFrameStatus);
    animationFrameStatus = runDynamicAnimations(animationFrameStatus);
    animationFrameStatus = runActiveStateAnimation(animationFrameStatus);
    runFadeAnimations(animationFrameStatus);
}

// unsigned long startTime = 0;
// unsigned long endTime = 0;

void loop()
{
    if (canReadKnockInput() && checkDidKnock()) // Light strip animations seem to make knock sensor readings higher (???) - may be due to current draw
    {
        start_animation(&fillAndShimmer_bases);
    }
    runAnimationFrame();

    if (iterationCount % 4 == 0)
    {

        // FastLED.show();
        // endTime = millis();
        // Serial.println("v delta v");
        // Serial.println(millis() - endTime);
    }

    // delayMicroseconds(1000);
    delay(10);
    iterationCount++;
}