#pragma once

#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <cstring>
#include <functional>

namespace livedaw::ui
{
/** Keeps a picture of artwork that rarely changes (a panel's silkscreen, its gradients and
    legends) and draws it as an image, so a repaint costs a copy instead of a few hundred
    gradients, paths and lines of text. The picture is made again when the component's size,
    the display's scale or `key` changes: `key` stands for everything else the artwork
    depends on (the theme, a finish, a lit lamp), see `PaintCache::key`. Message thread only.

    Where the picture could not match what drawing it directly makes (the component or
    something above it is transformed, or it sits between physical pixels) `paint` draws
    directly. The image is as the renderer prefers for temporary images, so a blit stays
    on the graphics card where there is one. */
class PaintCache
{
public:
    /** Folds values into a key (anything that converts to a 64-bit integer, or a colour). */
    template <typename... Values>
    static juce::uint64 key (const Values&... values)
    {
        juce::uint64 h = 1469598103934665603ull;
        ((h = (h ^ toBits (values)) * 1099511628211ull), ...);
        return h;
    }

    /** A key for where a component's children are and whether they show, for artwork laid out around them. */
    static juce::uint64 layoutKey (const juce::Component& c)
    {
        juce::uint64 h = 1469598103934665603ull;
        for (const auto* child : c.getChildren())
        {
            const auto b = child->getBounds();
            h = key (h, b.getX(), b.getY(), b.getWidth(), b.getHeight(), child->isVisible());
        }
        return h;
    }

    /** Draws `draw (graphics)` into the component's area from the picture, making it first
        when it is not there or out of date. `opaque`: the drawing covers the whole area. */
    template <typename Draw>
    void paint (juce::Graphics& g, const juce::Component& owner, juce::uint64 key, Draw&& draw, bool opaque = true)
    {
        const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
        const int w = owner.getWidth(), h = owner.getHeight();
        if (w < 1 || h < 1 || ! usable (owner, scale))
        {
            image = {};
            draw (g);
            return;
        }

        const int pw = juce::jmax (1, juce::roundToInt ((float) w * scale)), ph = juce::jmax (1, juce::roundToInt ((float) h * scale));
        if (image.isNull() || image.getWidth() != pw || image.getHeight() != ph || madeAt != key || madeScale != scale || madeOpaque != opaque)
        {
            const auto type = g.getInternalContext().getPreferredImageTypeForTemporaryImages();
            image = juce::Image (opaque ? juce::Image::RGB : juce::Image::ARGB, pw, ph, true, *type);
            juce::Graphics ig (image);
            if (scale != 1.0f)
                ig.addTransform (juce::AffineTransform::scale (scale));
            draw (ig);
            madeAt = key;
            madeScale = scale;
            madeOpaque = opaque;
        }

        if (scale == 1.0f)
            g.drawImageAt (image, 0, 0);
        else
            g.drawImageTransformed (image, juce::AffineTransform::scale (1.0f / scale));
    }

    /** Throws the picture away (the next paint makes it again). */
    void invalidate() { image = {}; }

private:
    static juce::uint64 toBits (juce::Colour c) noexcept { return c.getARGB(); }
    static juce::uint64 toBits (const juce::String& s) noexcept { return (juce::uint64) s.hashCode64(); }
    static juce::uint64 toBits (const char* s) noexcept { return (juce::uint64) juce::String (s).hashCode64(); }
    static juce::uint64 toBits (float v) noexcept
    {
        juce::uint32 bits = 0;
        static_assert (sizeof (bits) == sizeof (v));
        std::memcpy (&bits, &v, sizeof (bits));
        return bits;
    }
    static juce::uint64 toBits (double v) noexcept { return toBits ((float) v); }
    template <typename T>
    static juce::uint64 toBits (const T& v) noexcept
    {
        return (juce::uint64) v;
    }

    /** True where the picture lands on whole physical pixels with no transform in between. */
    static bool usable (const juce::Component& owner, float scale)
    {
        for (const auto* c = &owner; c != nullptr; c = c->getParentComponent())
            if (c->isTransformed())
                return false;
        if (owner.getParentComponent() == nullptr)
            return true;
        const auto* top = owner.getTopLevelComponent();
        const auto at = top->getLocalPoint (&owner, juce::Point<int>());
        const auto onGrid = [scale] (int v) { return std::abs ((float) v * scale - std::round ((float) v * scale)) < 0.001f; };
        return onGrid (at.x) && onGrid (at.y);
    }

    juce::Image image;
    juce::uint64 madeAt = 0;
    float madeScale = 0.0f;
    bool madeOpaque = true;
};
} // namespace livedaw::ui
