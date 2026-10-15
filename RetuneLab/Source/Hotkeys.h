#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_data_structures/juce_data_structures.h>
#include <array>

enum class Act { Undo = 0, SelectAll, Delete, SnapScale, SnapSemitone, Merge, Reset, SplitAtPlayhead,
                 EditTool, SplitTool, Record, FitView, Count, None };

// User-configurable shortcuts, stored in a settings file shared by every RetuneLab instance.
class HotkeyMap
{
public:
    static constexpr int N = (int) Act::Count;

    static juce::String label (int i)
    {
        static const char* names[N] = { "Undo", "Select all", "Delete / restore notes", "Snap to scale", "Snap to semitone",
                                        "Merge with next note", "Reset notes", "Split at playhead", "Edit tool",
                                        "Split tool", "Record on/off", "Fit view" };
        return names[i];
    }
    static juce::String defaultDesc (int i)
    {
        static const char* d[N] = { "command + Z", "command + A", "delete", "Q", "W", "J", "X", "K", "E", "S", "R", "F" };
        return d[i];
    }

    HotkeyMap() { load(); }

    juce::KeyPress get (int i) const { return keys[(size_t) i]; }
    juce::String describe (int i) const { return keys[(size_t) i].isValid() ? keys[(size_t) i].getTextDescription() : juce::String ("-"); }

    void set (int i, const juce::KeyPress& k)
    {
        for (size_t j = 0; j < keys.size(); ++j)
            if ((int) j != i && k.isValid() && keys[j] == k) keys[j] = juce::KeyPress();   // one key, one action
        keys[(size_t) i] = k;
        save();
    }
    void resetDefaults()
    {
        for (int i = 0; i < N; ++i) keys[(size_t) i] = juce::KeyPress::createFromDescription (defaultDesc (i));
        save();
    }
    Act match (const juce::KeyPress& k) const
    {
        for (int i = 0; i < N; ++i)
            if (keys[(size_t) i].isValid() && keys[(size_t) i] == k) return (Act) i;
        return Act::None;
    }

private:
    static juce::PropertiesFile::Options opts()
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "RetuneLab";
        o.filenameSuffix = ".settings";
        o.folderName = "RetuneLab";
        o.osxLibrarySubFolder = "Application Support";
        o.storageFormat = juce::PropertiesFile::storeAsXML;
        return o;
    }
    void load()
    {
        juce::PropertiesFile pf (opts());
        for (int i = 0; i < N; ++i)
            keys[(size_t) i] = juce::KeyPress::createFromDescription (pf.getValue ("hk" + juce::String (i), defaultDesc (i)));
    }
    void save()
    {
        juce::PropertiesFile pf (opts());
        for (int i = 0; i < N; ++i)
            pf.setValue ("hk" + juce::String (i), keys[(size_t) i].isValid() ? keys[(size_t) i].getTextDescription() : juce::String());
        pf.saveIfNeeded();
    }
    std::array<juce::KeyPress, (size_t) N> keys;
};
