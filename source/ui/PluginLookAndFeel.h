#pragma once

#include "LookAndFeelBase.h"

/**
    This plugin's look.

    Everything it draws, it draws the house way -- so this adds nothing to
    LookAndFeelBase today and exists to give it somewhere to disagree tomorrow. When the
    kit moves to its own repository the base goes with it and this file stays here.
*/
class PluginLookAndFeel : public LookAndFeelBase
{
public:
    using LookAndFeelBase::LookAndFeelBase;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginLookAndFeel)
};
