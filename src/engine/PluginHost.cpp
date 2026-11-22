#include "engine/PluginHost.h"
#include "engine/PluginText.h"
#include "engine/PluginScanner.h"

#include <map>
#include <memory>
#include <mutex>

namespace livedaw
{

static constexpr const char* kPluginListKey = "pluginList";
static constexpr const char* kSandboxKey = "plugins.sandbox";

namespace
{
    /** The descriptions in-process plugins were created from, by instance (see
        PluginHost::describe). An entry is only trusted while the instance at that
        address has the same name (a deleted plugin's address may be reused).
        Deleted with the app's other singletons at shutdown. */
    struct CreatedPlugins final : private juce::DeletedAtShutdown
    {
        ~CreatedPlugins() override { clearSingletonInstance(); }

        std::mutex lock;
        std::map<const juce::AudioPluginInstance*, juce::PluginDescription> descriptions;

        JUCE_DECLARE_SINGLETON_INLINE (CreatedPlugins, true)
    };

   #if JUCE_INTERNAL_HAS_LV2
    /** JUCE's LV2 format, made when it is first needed. Making it loads the LV2 folders and writes JUCE's own
        bundles to a temporary folder, about 0.15 s at every start-up, and a run that doesn't use LV2 never needs it. */
    class LazyLv2Format final : public juce::AudioPluginFormat
    {
    public:
        juce::String getName() const override { return juce::LV2PluginFormat::getFormatName(); }
        void findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>& results, const juce::String& id) override { real().findAllTypesForFile (results, id); }
        bool fileMightContainThisPluginType (const juce::String& id) override { return real().fileMightContainThisPluginType (id); }
        juce::String getNameOfPluginFromIdentifier (const juce::String& id) override { return real().getNameOfPluginFromIdentifier (id); }
        bool pluginNeedsRescanning (const juce::PluginDescription& d) override { return real().pluginNeedsRescanning (d); }
        bool doesPluginStillExist (const juce::PluginDescription& d) override { return real().doesPluginStillExist (d); }
        bool canScanForPlugins() const override { return real().canScanForPlugins(); }
        bool isTrivialToScan() const override { return real().isTrivialToScan(); }
        juce::StringArray searchPathsForPlugins (const juce::FileSearchPath& paths, bool recursive, bool asynchronous) override
        {
            return real().searchPathsForPlugins (paths, recursive, asynchronous);
        }
        juce::FileSearchPath getDefaultLocationsToSearch() override { return real().getDefaultLocationsToSearch(); }
        bool requiresUnblockedMessageThreadDuringCreation (const juce::PluginDescription& d) const override
        {
            return real().requiresUnblockedMessageThreadDuringCreation (d);
        }

    private:
        void createPluginInstance (const juce::PluginDescription& d, double sampleRate, int blockSize, PluginCreationCallback callback) override
        {
            juce::String error;
            auto instance = real().createInstanceFromDescription (d, sampleRate, blockSize, error);
            callback (std::move (instance), error);
        }

        juce::AudioPluginFormat& real() const
        {
            std::call_once (made, [this] { format = std::make_unique<juce::LV2PluginFormat>(); });
            return *format;
        }

        mutable std::once_flag made;
        mutable std::unique_ptr<juce::AudioPluginFormat> format;
    };
   #endif

    /** What juce::addDefaultFormatsToManager adds, with the LV2 format made on first use. */
    void addFormats (juce::AudioPluginFormatManager& manager)
    {
       #if JUCE_INTERNAL_HAS_AU
        manager.addFormat (std::make_unique<juce::AudioUnitPluginFormat>());
       #endif
       #if JUCE_INTERNAL_HAS_VST
        manager.addFormat (std::make_unique<juce::VSTPluginFormat>());
       #endif
       #if JUCE_INTERNAL_HAS_VST3
        manager.addFormat (std::make_unique<juce::VST3PluginFormat>());
       #endif
       #if JUCE_INTERNAL_HAS_LADSPA
        manager.addFormat (std::make_unique<juce::LADSPAPluginFormat>());
       #endif
       #if JUCE_INTERNAL_HAS_LV2
        manager.addFormat (std::make_unique<LazyLv2Format>());
       #endif
    }
} // namespace

PluginHost::PluginHost (juce::PropertiesFile* s) : settings (s)
{
    addFormats (formatManager);

    if (settings != nullptr)
        if (auto xml = settings->getXmlValue (kPluginListKey))
            knownPlugins.recreateFromXml (*xml);

    knownPlugins.addChangeListener (this);

    // A plugin that crashes during scanning only takes down the scanner process.
    knownPlugins.setCustomScanner (std::make_unique<OutOfProcessScanner>());

    SandboxOptions options;
    options.helper = SandboxedPlugin::findHelper();
    sandboxOptions = std::make_shared<const SandboxOptions> (std::move (options));
    sandboxEnabled = settings != nullptr && settings->getBoolValue (kSandboxKey, false);
}

void PluginHost::setSandboxEnabled (bool on)
{
    sandboxEnabled = on;
    if (settings != nullptr)
    {
        settings->setValue (kSandboxKey, on);
        settings->saveIfNeeded();
    }
}

void PluginHost::setSandboxOptions (const SandboxOptions& options)
{
    sandboxOptions = std::make_shared<const SandboxOptions> (options);
}

PluginHost::~PluginHost()
{
    knownPlugins.removeChangeListener (this);
}

juce::File PluginHost::getDeadMansPedalFile() const
{
    if (settings != nullptr)
        return settings->getFile().getSiblingFile ("RecentlyCrashedPluginsList");

    return juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("LiveDAWCrashedPlugins");
}

void PluginHost::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (settings == nullptr)
        return;

    if (auto xml = knownPlugins.createXml())
    {
        settings->setValue (kPluginListKey, xml.get());
        settings->saveIfNeeded();
    }
}

juce::File PluginHost::getPendingLoadFile() const
{
    return getDeadMansPedalFile().getSiblingFile ("PluginBeingLoaded");
}

juce::String PluginHost::checkForCrashDuringPluginLoad()
{
    const auto file = getPendingLoadFile();

    if (! file.existsAsFile())
        return {};

    const auto lines = juce::StringArray::fromLines (file.loadFileAsString());
    file.deleteFile();

    const auto identifier = lines[0].trim();
    if (identifier.isEmpty())
        return {};

    knownPlugins.addToBlacklist (identifier);
    ScanFailures::record (identifier, "it crashed while it was being loaded last time");
    return lines[1].isNotEmpty() ? lines[1] : identifier;
}

juce::PluginDescription PluginHost::resolveDescription (const juce::PluginDescription& description)
{
    // On Linux a VST3 instance describes itself with the library inside its
    // bundle (X.vst3/Contents/x86_64-linux/X.so), which no format accepts, and
    // shows saved that: load the bundle instead.
    if (description.pluginFormatName != "VST3" || ! juce::File::isAbsolutePath (description.fileOrIdentifier))
        return description;
    const juce::File file (description.fileOrIdentifier);
    if (file.hasFileExtension (".vst3"))
        return description;
    for (auto dir = file.getParentDirectory(); dir != dir.getParentDirectory(); dir = dir.getParentDirectory())
    {
        if (dir.hasFileExtension (".vst3"))
        {
            auto resolved = description;
            resolved.fileOrIdentifier = dir.getFullPathName();
            return resolved;
        }
    }
    return description;
}

std::unique_ptr<juce::AudioPluginInstance> PluginHost::createInstance (const juce::PluginDescription& requested,
                                                                       double sampleRate, int blockSize,
                                                                       juce::String& errorMessage)
{
    const auto description = resolveDescription (requested);

    if (sandboxEnabled)
    {
        // A crash while loading only takes down the helper: no marker needed.
        auto sandboxed = SandboxedPlugin::create (description, sampleRate, blockSize, sandboxOptions, errorMessage);
        if (sandboxed == nullptr && errorMessage.isNotEmpty())
            errorMessage = "in the plugin sandbox: " + errorMessage;
        return sandboxed;
    }

    const auto pending = getPendingLoadFile();
    pending.getParentDirectory().createDirectory();
    pending.replaceWithText (description.fileOrIdentifier + "\n" + description.name);

    auto instance = formatManager.createPluginInstance (description, sampleRate, blockSize, errorMessage);

    pending.deleteFile();
    if (auto* created = instance != nullptr ? CreatedPlugins::getInstance() : nullptr)
    {
        const std::lock_guard<std::mutex> l (created->lock);
        created->descriptions[instance.get()] = description;
    }
    return instance;
}

juce::PluginDescription PluginHost::describe (const juce::AudioPluginInstance& plugin)
{
    if (auto* created = CreatedPlugins::getInstanceWithoutCreating())
    {
        const std::lock_guard<std::mutex> l (created->lock);
        if (const auto it = created->descriptions.find (&plugin); it != created->descriptions.end())
        {
            // A sandboxed plugin reports its name cleaned (PluginText.h).
            if (it->second.name == plugin.getName() || cleanPluginText (it->second.name) == plugin.getName())
                return it->second;
            created->descriptions.erase (it); // another plugin now lives there
        }
    }
    return plugin.getPluginDescription();
}

juce::Array<juce::PluginDescription> PluginHost::getSortedTypes() const
{
    auto types = knownPlugins.getTypes();
    std::sort (types.begin(), types.end(), [] (const auto& a, const auto& b)
    {
        if (a.manufacturerName != b.manufacturerName)
            return a.manufacturerName.compareIgnoreCase (b.manufacturerName) < 0;
        return a.name.compareIgnoreCase (b.name) < 0;
    });
    return types;
}

} // namespace livedaw
