package androidx.compose.ui

// Build-mechanism shim - see androidx/lifecycle/runtime/R.kt's comment for the full explanation.
// Distinct from androidx.compose.ui.R$string (a separate nested class, shimmed alongside this one
// in this same file) and from androidx.core.R$id / androidx.compose.ui.graphics.R$id (different
// classes entirely, despite similarly-named fields - View.setTag(int,Object) keys only need to be
// distinct integers, not globally unique names, so accidental value overlap across these different
// R$id classes would not actually collide at runtime either way, but distinct base offsets are used
// throughout these shims for clarity). Fields determined by decompiling
// androidx.compose.ui.platform.AndroidComposeView's constructor and related platform classes
// (the exact methods R8's "missing class" error cited) rather than guessed.
class R {
    object id {
        const val androidx_compose_ui_view_compose_view_context: Int = 0x7f0d1fff
        const val androidx_compose_ui_view_composition_context: Int = 0x7f0d1ffe
        const val compose_view_saveable_id_tag: Int = 0x7f0d1ffd
        const val hide_in_inspector_tag: Int = 0x7f0d2000
        const val auto_clear_focus_behavior_tag: Int = 0x7f0d2001
        const val inspection_slot_table_set: Int = 0x7f0d2002
        const val wrapped_composition_tag: Int = 0x7f0d2003
        const val accessibility_custom_action_0: Int = 0x7f0d2004
        const val accessibility_custom_action_1: Int = 0x7f0d2005
        const val accessibility_custom_action_2: Int = 0x7f0d2006
        const val accessibility_custom_action_3: Int = 0x7f0d2007
        const val accessibility_custom_action_4: Int = 0x7f0d2008
        const val accessibility_custom_action_5: Int = 0x7f0d2009
        const val accessibility_custom_action_6: Int = 0x7f0d200a
        const val accessibility_custom_action_7: Int = 0x7f0d200b
        const val accessibility_custom_action_8: Int = 0x7f0d200c
        const val accessibility_custom_action_9: Int = 0x7f0d200d
        const val accessibility_custom_action_10: Int = 0x7f0d200e
        const val accessibility_custom_action_11: Int = 0x7f0d200f
        const val accessibility_custom_action_12: Int = 0x7f0d2010
        const val accessibility_custom_action_13: Int = 0x7f0d2011
        const val accessibility_custom_action_14: Int = 0x7f0d2012
        const val accessibility_custom_action_15: Int = 0x7f0d2013
        const val accessibility_custom_action_16: Int = 0x7f0d2014
        const val accessibility_custom_action_17: Int = 0x7f0d2015
        const val accessibility_custom_action_18: Int = 0x7f0d2016
        const val accessibility_custom_action_19: Int = 0x7f0d2017
        const val accessibility_custom_action_20: Int = 0x7f0d2018
        const val accessibility_custom_action_21: Int = 0x7f0d2019
        const val accessibility_custom_action_22: Int = 0x7f0d201a
        const val accessibility_custom_action_23: Int = 0x7f0d201b
        const val accessibility_custom_action_24: Int = 0x7f0d201c
        const val accessibility_custom_action_25: Int = 0x7f0d201d
        const val accessibility_custom_action_26: Int = 0x7f0d201e
        const val accessibility_custom_action_27: Int = 0x7f0d201f
        const val accessibility_custom_action_28: Int = 0x7f0d2020
        const val accessibility_custom_action_29: Int = 0x7f0d2021
        const val accessibility_custom_action_30: Int = 0x7f0d2022
        const val accessibility_custom_action_31: Int = 0x7f0d2023
    }

    object string {
        const val tab: Int = 0x7f0e0001
        const val switch_role: Int = 0x7f0e0002
    }
}
