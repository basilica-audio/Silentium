#include "PresetManager.h"

#include <algorithm>

// JSON preset file format ("basilica-preset-1"):
//
//   {
//     "format": "basilica-preset-1",
//     "plugin": "com.yvesvogl.nave",
//     "pluginVersion": "0.2.0",
//     "name": "Preset Name",
//     "category": "Init|Guitar|...",
//     "parameters": { "<paramID>": <plain (unnormalised) value>, ... }
//   }
//
// See .scaffold/specs/preset-system-m2.md for the full binding spec this
// implements.

namespace basilica::presets
{
    namespace
    {
        constexpr const char* formatKey = "format";
        constexpr const char* pluginKey = "plugin";
        constexpr const char* pluginVersionKey = "pluginVersion";
        constexpr const char* nameKey = "name";
        constexpr const char* categoryKey = "category";
        constexpr const char* parametersKey = "parameters";
        constexpr const char* defaultPresetName = "Default";

        juce::String legalFileStem (const juce::String& name)
        {
            return juce::File::createLegalFileName (name);
        }
    }

    //==========================================================================
    PresetManager::PresetManager (juce::AudioProcessorValueTreeState& stateToControl,
                                   PresetManagerConfig configToUse,
                                   std::vector<FactoryPresetAsset> factoryAssets)
        : apvts (stateToControl), config (std::move (configToUse))
    {
        for (auto& asset : factoryAssets)
        {
            if (asset.data == nullptr || asset.dataSize <= 0)
                continue;

            const auto jsonText = juce::String::fromUTF8 (asset.data, asset.dataSize);
            juce::String errorMessage;
            const auto parsed = parseAndValidate (jsonText, errorMessage);

            if (parsed.isVoid())
            {
                // A factory preset asset failing to parse/validate is a
                // build-time authoring bug (a malformed presets/factory/*.json
                // file shipped in BinaryData), not a runtime condition a user
                // can hit - surfaced loudly in debug builds, skipped safely
                // in release builds rather than crashing the plugin over a
                // missing preset.
                jassertfalse;
                continue;
            }

            auto* obj = parsed.getDynamicObject();
            factoryPresets.push_back ({ obj->getProperty (nameKey).toString(),
                                         obj->getProperty (categoryKey).toString(),
                                         parsed });
        }

        // Adopt anything still sitting in the pre-rename manufacturer folder
        // before the first getAllPresets()/applyStartupDefault() can look, so a
        // user's own "Default" survives the rename on the very first launch.
        migrateLegacyUserPresets();

        for (auto* parameter : apvts.processor.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
                apvts.addParameterListener (ranged->paramID, this);
    }

    PresetManager::~PresetManager()
    {
        for (auto* parameter : apvts.processor.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
                apvts.removeParameterListener (ranged->paramID, this);
    }

    //==========================================================================
    void PresetManager::parameterChanged (const juce::String&, float)
    {
        // Real-time safe by design - see the class-level docs in
        // PresetManager.h. Nothing else happens in this callback.
        if (! applyingPreset.load (std::memory_order_relaxed))
            dirty.store (true, std::memory_order_relaxed);
    }

    void PresetManager::applyStartupDefault()
    {
        loadPreset (defaultPresetName);
    }

    //==========================================================================
    void PresetManager::resetAllParametersToDefault()
    {
        for (auto* parameter : apvts.processor.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
                ranged->setValueNotifyingHost (ranged->getDefaultValue());
    }

    void PresetManager::applyPlainValues (const juce::var& parametersObject)
    {
        auto* obj = parametersObject.getDynamicObject();

        if (obj == nullptr)
            return;

        for (auto& property : obj->getProperties())
        {
            // Unknown parameter IDs are silently ignored - forward-tolerant
            // import (a preset saved by a newer plugin version carrying a
            // parameter this build doesn't know about).
            auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (property.name.toString()));

            if (ranged != nullptr)
                ranged->setValueNotifyingHost (ranged->convertTo0to1 (static_cast<float> (property.value)));
        }
    }

    void PresetManager::applyParsedPreset (const juce::var& parsed, const juce::String& name, bool isFactory)
    {
        applyingPreset.store (true, std::memory_order_relaxed);

        resetAllParametersToDefault();

        auto* obj = parsed.getDynamicObject();
        jassert (obj != nullptr); // parseAndValidate() guarantees this

        applyPlainValues (obj->getProperty (parametersKey));

        currentPresetName = name;
        currentPresetIsFactory = isFactory;

        applyingPreset.store (false, std::memory_order_relaxed);
        dirty.store (false, std::memory_order_relaxed);
    }

    //==========================================================================
    juce::var PresetManager::parseAndValidate (const juce::String& jsonText, juce::String& errorMessage) const
    {
        juce::var parsed;
        const auto parseResult = juce::JSON::parse (jsonText, parsed);

        if (parseResult.failed() || ! parsed.isObject())
        {
            errorMessage = TRANS ("This file is not a valid preset.");
            return {};
        }

        auto* obj = parsed.getDynamicObject();

        if (obj->getProperty (formatKey).toString() != juce::String (presetFormatTag))
        {
            errorMessage = TRANS ("This preset was saved by an incompatible version of the preset format.");
            return {};
        }

        if (obj->getProperty (pluginKey).toString() != config.pluginId)
        {
            errorMessage = TRANS ("This preset file belongs to a different plugin.");
            return {};
        }

        return parsed;
    }

    juce::var PresetManager::buildPresetVar (const juce::String& name, const juce::String& category) const
    {
        auto* parametersObj = new juce::DynamicObject();

        for (auto* parameter : apvts.processor.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
                parametersObj->setProperty (ranged->paramID, ranged->convertFrom0to1 (ranged->getValue()));

        auto* obj = new juce::DynamicObject();
        obj->setProperty (formatKey, juce::String (presetFormatTag));
        obj->setProperty (pluginKey, config.pluginId);
        obj->setProperty (pluginVersionKey, config.pluginVersion);
        obj->setProperty (nameKey, name);
        obj->setProperty (categoryKey, category);
        obj->setProperty (parametersKey, juce::var (parametersObj));

        return juce::var (obj);
    }

    bool PresetManager::writePresetVarToFile (const juce::var& presetVar, const juce::File& destination) const
    {
        const auto json = juce::JSON::toString (presetVar, false);
        return destination.replaceWithText (json);
    }

    juce::File PresetManager::userPresetFileFor (const juce::String& name) const
    {
        return getUserPresetsDirectory (config).getChildFile (legalFileStem (name) + presetFileExtension);
    }

    //==========================================================================
    juce::File PresetManager::userPresetsDirectoryFor (const juce::String& manufacturer,
                                                        const juce::String& plugin)
    {
       #if JUCE_MAC
        return juce::File::getSpecialLocation (juce::File::userHomeDirectory)
            .getChildFile ("Library")
            .getChildFile ("Audio")
            .getChildFile ("Presets")
            .getChildFile (manufacturer)
            .getChildFile (plugin);
       #elif JUCE_WINDOWS
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
            .getChildFile (manufacturer)
            .getChildFile (plugin)
            .getChildFile ("Presets");
       #else
        // Linux/other: not a CI or release target for this suite (see
        // CLAUDE.md - CI is macOS + Windows only), but still a sane,
        // discoverable per-user location rather than an unsupported path.
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
            .getChildFile (manufacturer)
            .getChildFile (plugin)
            .getChildFile ("Presets");
       #endif
    }

    juce::File PresetManager::getUserPresetsDirectory (const PresetManagerConfig& presetManagerConfig)
    {
        if (presetManagerConfig.userPresetsDirectoryOverrideForTests != juce::File())
            return presetManagerConfig.userPresetsDirectoryOverrideForTests;

        return userPresetsDirectoryFor (presetManagerConfig.manufacturerName,
                                        presetManagerConfig.pluginName);
    }

    juce::File PresetManager::getLegacyUserPresetsDirectory (const PresetManagerConfig& presetManagerConfig)
    {
        if (presetManagerConfig.legacyUserPresetsDirectoryOverrideForTests != juce::File())
            return presetManagerConfig.legacyUserPresetsDirectoryOverrideForTests;

        // A config that redirects the *current* directory into a scratch folder
        // but says nothing about the legacy one must not fall through to the
        // real per-user location: a test would then read, and copy from, the
        // presets of whoever happens to be running it. An overridden current
        // directory with no legacy override means "there is no legacy
        // directory", not "use the real one".
        if (presetManagerConfig.userPresetsDirectoryOverrideForTests != juce::File())
            return {};

        if (presetManagerConfig.legacyManufacturerName.isEmpty()
             || presetManagerConfig.legacyManufacturerName == presetManagerConfig.manufacturerName)
            return {};

        return userPresetsDirectoryFor (presetManagerConfig.legacyManufacturerName,
                                        presetManagerConfig.pluginName);
    }

    int PresetManager::migrateLegacyUserPresets()
    {
        const auto legacyDirectory = getLegacyUserPresetsDirectory (config);

        // The overwhelmingly common case - a machine that never saw the old
        // folder - costs exactly this one filesystem check.
        if (legacyDirectory == juce::File() || ! legacyDirectory.isDirectory())
            return 0;

        const auto currentDirectory = getUserPresetsDirectory (config);

        if (legacyDirectory == currentDirectory)
            return 0;

        const auto legacyFiles = legacyDirectory.findChildFiles (
            juce::File::findFiles, false, juce::String ("*") + presetFileExtension);

        if (legacyFiles.isEmpty())
            return 0;

        if (! currentDirectory.isDirectory() && ! currentDirectory.createDirectory().wasOk())
            return 0;

        int copied = 0;

        for (auto& legacyFile : legacyFiles)
        {
            const auto destination = currentDirectory.getChildFile (legacyFile.getFileName());

            // Never overwrite: a file already at the new location is either one
            // the user saved after the rename or one this migration copied and
            // the user has since edited. Either way it wins over whatever the
            // legacy folder still holds.
            if (destination.existsAsFile())
                continue;

            if (legacyFile.copyFileTo (destination))
                ++copied;
        }

        return copied;
    }

    //==========================================================================
    std::vector<PresetManager::PresetEntry> PresetManager::getAllPresets() const
    {
        std::vector<PresetEntry> factoryEntries;

        for (auto& factory : factoryPresets)
            factoryEntries.push_back ({ factory.name, factory.category, true });

        std::sort (factoryEntries.begin(), factoryEntries.end(),
                   [] (const PresetEntry& a, const PresetEntry& b) { return a.name < b.name; });

        std::vector<PresetEntry> userEntries;
        const auto userFiles = getUserPresetsDirectory (config).findChildFiles (
            juce::File::findFiles, false, juce::String ("*") + presetFileExtension);

        for (auto& file : userFiles)
        {
            juce::String errorMessage;
            const auto parsed = parseAndValidate (file.loadFileAsString(), errorMessage);

            if (parsed.isVoid())
                continue;

            auto* obj = parsed.getDynamicObject();
            userEntries.push_back ({ obj->getProperty (nameKey).toString(), obj->getProperty (categoryKey).toString(), false });
        }

        std::sort (userEntries.begin(), userEntries.end(),
                   [] (const PresetEntry& a, const PresetEntry& b) { return a.name < b.name; });

        factoryEntries.insert (factoryEntries.end(), userEntries.begin(), userEntries.end());
        return factoryEntries;
    }

    //==========================================================================
    bool PresetManager::loadPreset (const juce::String& name)
    {
        // User presets win over factory presets of the same name - the same
        // resolution order applyStartupDefault() relies on for "Default".
        const auto userFile = userPresetFileFor (name);

        if (userFile.existsAsFile())
        {
            juce::String errorMessage;
            const auto parsed = parseAndValidate (userFile.loadFileAsString(), errorMessage);

            if (! parsed.isVoid())
            {
                applyParsedPreset (parsed, name, false);
                return true;
            }
        }

        for (auto& factory : factoryPresets)
        {
            if (factory.name == name)
            {
                applyParsedPreset (factory.parsed, name, true);
                return true;
            }
        }

        return false;
    }

    void PresetManager::nextPreset()
    {
        const auto all = getAllPresets();

        if (all.empty())
            return;

        const auto it = std::find_if (all.begin(), all.end(),
                                       [this] (const PresetEntry& e) { return e.name == currentPresetName; });

        const auto currentIndex = (it == all.end()) ? static_cast<size_t> (-1)
                                                      : static_cast<size_t> (std::distance (all.begin(), it));
        const auto nextIndex = (it == all.end()) ? size_t { 0 } : (currentIndex + 1) % all.size();

        loadPreset (all[nextIndex].name);
    }

    void PresetManager::previousPreset()
    {
        const auto all = getAllPresets();

        if (all.empty())
            return;

        const auto it = std::find_if (all.begin(), all.end(),
                                       [this] (const PresetEntry& e) { return e.name == currentPresetName; });

        size_t previousIndex = 0;

        if (it != all.end())
        {
            const auto currentIndex = static_cast<size_t> (std::distance (all.begin(), it));
            previousIndex = (currentIndex == 0) ? all.size() - 1 : currentIndex - 1;
        }
        else
        {
            previousIndex = all.size() - 1;
        }

        loadPreset (all[previousIndex].name);
    }

    //==========================================================================
    bool PresetManager::saveUserPreset (const juce::String& name, const juce::String& category)
    {
        for (auto& factory : factoryPresets)
            if (factory.name == name)
                return false;

        const auto dir = getUserPresetsDirectory (config);

        if (! dir.exists() && ! dir.createDirectory())
            return false;

        const auto presetVar = buildPresetVar (name, category);

        if (! writePresetVarToFile (presetVar, userPresetFileFor (name)))
            return false;

        currentPresetName = name;
        currentPresetIsFactory = false;
        dirty.store (false, std::memory_order_relaxed);
        return true;
    }

    bool PresetManager::saveCurrentUserPreset()
    {
        if (currentPresetIsFactory || currentPresetName.isEmpty())
            return false;

        juce::String category = "Init";
        const auto existingFile = userPresetFileFor (currentPresetName);

        if (existingFile.existsAsFile())
        {
            juce::String errorMessage;
            const auto parsed = parseAndValidate (existingFile.loadFileAsString(), errorMessage);

            if (! parsed.isVoid())
                category = parsed.getDynamicObject()->getProperty (categoryKey).toString();
        }

        return saveUserPreset (currentPresetName, category);
    }

    bool PresetManager::renameUserPreset (const juce::String& oldName, const juce::String& newName)
    {
        const auto oldFile = userPresetFileFor (oldName);

        if (! oldFile.existsAsFile())
            return false;

        juce::String errorMessage;
        const auto parsed = parseAndValidate (oldFile.loadFileAsString(), errorMessage);

        if (parsed.isVoid())
            return false;

        auto* obj = parsed.getDynamicObject();
        const auto renamed = buildPresetVar (newName, obj->getProperty (categoryKey).toString());

        // buildPresetVar() above stamps the *current live* APVTS values, not
        // necessarily the renamed preset's own saved values - overwrite its
        // parameters with the original file's, so a rename never silently
        // mutates the preset's content.
        renamed.getDynamicObject()->setProperty (parametersKey, obj->getProperty (parametersKey));

        if (! writePresetVarToFile (renamed, userPresetFileFor (newName)))
            return false;

        oldFile.deleteFile();

        if (currentPresetName == oldName)
            currentPresetName = newName;

        return true;
    }

    bool PresetManager::deleteUserPreset (const juce::String& name)
    {
        const auto file = userPresetFileFor (name);

        if (! file.existsAsFile())
            return false;

        const auto deleted = file.deleteFile();

        if (deleted && currentPresetName == name)
            currentPresetName.clear();

        return deleted;
    }

    //==========================================================================
    bool PresetManager::setCurrentAsDefault()
    {
        const auto dir = getUserPresetsDirectory (config);

        if (! dir.exists() && ! dir.createDirectory())
            return false;

        // Deliberately bypasses saveUserPreset()'s "can't shadow a factory
        // name" guard and doesn't touch currentPresetName/dirty - see the
        // header docs.
        const auto presetVar = buildPresetVar (defaultPresetName, "Init");
        return writePresetVarToFile (presetVar, userPresetFileFor (defaultPresetName));
    }

    bool PresetManager::resetDefault()
    {
        const auto file = userPresetFileFor (defaultPresetName);

        if (! file.existsAsFile())
            return true; // nothing to reset is not an error

        return file.deleteFile();
    }

    //==========================================================================
    bool PresetManager::importPresetFile (const juce::File& file, juce::String& errorMessage)
    {
        const auto parsed = parseAndValidate (file.loadFileAsString(), errorMessage);

        if (parsed.isVoid())
            return false;

        applyParsedPreset (parsed, parsed.getDynamicObject()->getProperty (nameKey).toString(), false);
        return true;
    }

    bool PresetManager::exportPreset (const juce::String& name, const juce::File& destination) const
    {
        for (auto& factory : factoryPresets)
            if (factory.name == name)
                return writePresetVarToFile (factory.parsed, destination);

        const auto userFile = userPresetFileFor (name);

        if (! userFile.existsAsFile())
            return false;

        juce::String errorMessage;
        const auto parsed = parseAndValidate (userFile.loadFileAsString(), errorMessage);

        if (parsed.isVoid())
            return false;

        return writePresetVarToFile (parsed, destination);
    }

    //==========================================================================
    bool PresetManager::exportBank (const juce::File& destination, const std::vector<juce::String>& userPresetNamesOnly) const
    {
        const auto dir = getUserPresetsDirectory (config);
        juce::Array<juce::File> filesToInclude;

        if (userPresetNamesOnly.empty())
        {
            filesToInclude = dir.findChildFiles (juce::File::findFiles, false, juce::String ("*") + presetFileExtension);
        }
        else
        {
            for (auto& name : userPresetNamesOnly)
            {
                const auto file = userPresetFileFor (name);

                if (file.existsAsFile())
                    filesToInclude.add (file);
            }
        }

        if (filesToInclude.isEmpty())
            return false;

        juce::ZipFile::Builder builder;

        for (auto& file : filesToInclude)
            builder.addFile (file, 9, file.getFileName());

        // FileOutputStream appends to an existing file by default - delete
        // first so a re-export always produces a clean, exact bank rather
        // than a stale one with leftover trailing bytes.
        destination.deleteFile();

        std::unique_ptr<juce::FileOutputStream> outputStream (destination.createOutputStream());

        if (outputStream == nullptr)
            return false;

        return builder.writeToStream (*outputStream, nullptr);
    }

    int PresetManager::importBank (const juce::File& zipFile)
    {
        juce::ZipFile zip (zipFile);
        int importedCount = 0;

        const auto dir = getUserPresetsDirectory (config);

        if (! dir.exists() && ! dir.createDirectory())
            return 0;

        for (int i = 0; i < zip.getNumEntries(); ++i)
        {
            const auto* entry = zip.getEntry (i);

            if (entry == nullptr || ! entry->filename.endsWithIgnoreCase (presetFileExtension))
                continue;

            const std::unique_ptr<juce::InputStream> stream (zip.createStreamForEntry (i));

            if (stream == nullptr)
                continue;

            const auto text = stream->readEntireStreamAsString();
            juce::String errorMessage;
            const auto parsed = parseAndValidate (text, errorMessage);

            if (parsed.isVoid())
                continue; // wrong plugin / malformed entry - skip, don't abort the whole bank

            auto* obj = parsed.getDynamicObject();
            const auto name = obj->getProperty (nameKey).toString();

            if (writePresetVarToFile (parsed, userPresetFileFor (name)))
                ++importedCount;
        }

        return importedCount;
    }
}
