#include "ui/PluginPictures.h"

#include <algorithm>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
 #include <vector>
#endif

namespace livedaw::ui
{

namespace
{
#if JUCE_WINDOWS
    /** `area` (in `top`'s coordinates) of the window `handle`, drawn by the window
        itself into a bitmap: native child views included, covered or not. */
    juce::Image grabWindow (void* handle, const juce::Component& top, juce::Rectangle<int> area)
    {
        auto* hwnd = static_cast<HWND> (handle);
        RECT client {};
        if (hwnd == nullptr || ! GetClientRect (hwnd, &client) || top.getWidth() <= 0 || top.getHeight() <= 0)
            return {};
        const int cw = (int) client.right, ch = (int) client.bottom;
        if (cw <= 0 || ch <= 0)
            return {};
        // Physical pixels per logical one (the display's scale).
        const double sx = cw / (double) top.getWidth(), sy = ch / (double) top.getHeight();
        const auto px = juce::Rectangle<int> (juce::roundToInt (area.getX() * sx), juce::roundToInt (area.getY() * sy),
                                              juce::roundToInt (area.getWidth() * sx), juce::roundToInt (area.getHeight() * sy))
                            .getIntersection ({ 0, 0, cw, ch });
        if (px.isEmpty())
            return {};

        auto* windowDc = GetDC (hwnd);
        if (windowDc == nullptr)
            return {};
        auto* memoryDc = CreateCompatibleDC (windowDc);
        auto* bitmap = CreateCompatibleBitmap (windowDc, cw, ch);
        auto* previous = SelectObject (memoryDc, bitmap);
        constexpr UINT renderFullContent = 0x00000002; // PW_RENDERFULLCONTENT (Windows 8.1)
        const bool drawn = PrintWindow (hwnd, memoryDc, PW_CLIENTONLY | renderFullContent) != FALSE;
        SelectObject (memoryDc, previous);

        std::vector<juce::uint32> pixels;
        if (drawn)
        {
            BITMAPINFO info {};
            info.bmiHeader.biSize = sizeof (BITMAPINFOHEADER);
            info.bmiHeader.biWidth = cw;
            info.bmiHeader.biHeight = -ch; // top-down
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            pixels.resize ((size_t) cw * (size_t) ch);
            if (GetDIBits (memoryDc, bitmap, 0, (UINT) ch, pixels.data(), &info, DIB_RGB_COLORS) != ch)
                pixels.clear();
        }
        DeleteObject (bitmap);
        DeleteDC (memoryDc);
        ReleaseDC (hwnd, windowDc);
        if (pixels.empty())
            return {};

        // BGRA rows, as JUCE's ARGB images keep them; GDI leaves alpha at 0.
        juce::Image image (juce::Image::ARGB, px.getWidth(), px.getHeight(), false);
        juce::Image::BitmapData data (image, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < px.getHeight(); ++y)
        {
            auto* line = reinterpret_cast<juce::uint32*> (data.getLinePointer (y));
            const auto* source = pixels.data() + (size_t) (px.getY() + y) * (size_t) cw + (size_t) px.getX();
            for (int x = 0; x < px.getWidth(); ++x)
                line[x] = source[x] | 0xff000000u;
        }
        return image;
    }
#endif

    /** True if every pixel is black: a window that didn't draw into the bitmap (an editor of one
        other colour is a real picture). */
    bool isBlank (const juce::Image& image)
    {
        if (image.isNull())
            return true;
        const auto black = juce::Colours::black;
        const int stepX = juce::jmax (1, image.getWidth() / 32), stepY = juce::jmax (1, image.getHeight() / 32);
        for (int y = 0; y < image.getHeight(); y += stepY)
            for (int x = 0; x < image.getWidth(); x += stepX)
                if (image.getPixelAt (x, y).withAlpha (1.0f) != black)
                    return false;
        return true;
    }
} // namespace

void PluginPictures::store (const juce::AudioProcessor& plugin, juce::Image image)
{
    if (image.isNull())
        return;
    pictures[&plugin] = { std::move (image), juce::Time::getCurrentTime(), plugin.getName() };
    ++generation;
}

const PluginPictures::Picture* PluginPictures::find (const juce::AudioProcessor* plugin) const
{
    const auto it = pictures.find (plugin);
    if (it == pictures.end() || it->second.name != plugin->getName())
        return nullptr;
    return &it->second;
}

void PluginPictures::forget (const juce::AudioProcessor* plugin)
{
    if (pictures.erase (plugin) > 0)
        ++generation;
}

void PluginPictures::retainOnly (const std::vector<const juce::AudioProcessor*>& live)
{
    for (auto it = pictures.begin(); it != pictures.end();)
    {
        if (std::find (live.begin(), live.end(), it->first) == live.end())
        {
            it = pictures.erase (it);
            ++generation;
        }
        else
            ++it;
    }
}

juce::Image PluginPictures::grab (juce::Component& editor, int maxWidth)
{
    auto* peer = editor.getPeer();
    if (peer == nullptr || ! editor.isShowing() || peer->isMinimised())
        return {};
    // Its part inside every parent (a viewport scrolls it, a card clips it).
    auto area = editor.getLocalBounds();
    for (auto* p = editor.getParentComponent(); p != nullptr && ! area.isEmpty(); p = p->getParentComponent())
        area = area.getIntersection (editor.getLocalArea (p, p->getLocalBounds()));
    if (area.isEmpty())
        return {};

    juce::Image image;
   #if JUCE_WINDOWS
    auto& top = peer->getComponent();
    image = grabWindow (peer->getNativeHandle(), top, top.getLocalArea (&editor, area));
   #endif
    if (isBlank (image))
        image = editor.createComponentSnapshot (area, true, 1.0f);
    if (image.isNull())
        return {};
    if (image.getWidth() > maxWidth)
        image = image.rescaled (maxWidth, juce::jmax (1, image.getHeight() * maxWidth / image.getWidth()), juce::Graphics::highResamplingQuality);
    return image;
}

} // namespace livedaw::ui
