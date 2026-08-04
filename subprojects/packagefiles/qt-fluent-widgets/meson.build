project(
    'qt-fluent-widgets',
    'cpp',
    version: '1.0.0',
    meson_version: '>=1.4.0',
    default_options: ['cpp_std=c++17', 'warning_level=0'],
)

qt6 = import('qt6')
qt6_dep = dependency('qt6', modules: ['Widgets', 'Svg'], version: '>=6.5')
qfw_include = include_directories('qtfluentwidgets')
qfw_system_include = include_directories('qtfluentwidgets', is_system: true)
qfw_cpp = meson.get_compiler('cpp')
qfw_cpp_args = []
foreach warning : [
    '-Wno-deprecated-declarations',
    '-Wno-sfinae-incomplete',
    '-Wno-unused-result',
]
    if qfw_cpp.has_argument(warning)
        qfw_cpp_args += warning
    endif
endforeach

qfw_sources = files(
    'qtfluentwidgets/common/animation.cpp',
    'qtfluentwidgets/common/auto_wrap.cpp',
    'qtfluentwidgets/common/color.cpp',
    'qtfluentwidgets/common/config.cpp',
    'qtfluentwidgets/common/font.cpp',
    'qtfluentwidgets/common/icon.cpp',
    'qtfluentwidgets/common/router.cpp',
    'qtfluentwidgets/common/screen.cpp',
    'qtfluentwidgets/common/smooth_scroll.cpp',
    'qtfluentwidgets/common/style_sheet.cpp',
    'qtfluentwidgets/common/theme_listener.cpp',
    'qtfluentwidgets/common/translator.cpp',
    'qtfluentwidgets/components/date_time/calendar_picker.cpp',
    'qtfluentwidgets/components/date_time/calendar_view.cpp',
    'qtfluentwidgets/components/date_time/date_picker.cpp',
    'qtfluentwidgets/components/date_time/fast_calendar_picker.cpp',
    'qtfluentwidgets/components/date_time/fast_calendar_view.cpp',
    'qtfluentwidgets/components/date_time/picker_base.cpp',
    'qtfluentwidgets/components/date_time/time_picker.cpp',
    'qtfluentwidgets/components/dialog_box/color_dialog.cpp',
    'qtfluentwidgets/components/dialog_box/dialog.cpp',
    'qtfluentwidgets/components/dialog_box/folder_list_dialog.cpp',
    'qtfluentwidgets/components/dialog_box/mask_dialog_base.cpp',
    'qtfluentwidgets/components/dialog_box/message_box_base.cpp',
    'qtfluentwidgets/components/dialog_box/message_dialog.cpp',
    'qtfluentwidgets/components/layout/expand_layout.cpp',
    'qtfluentwidgets/components/layout/flow_layout.cpp',
    'qtfluentwidgets/components/layout/v_box_layout.cpp',
    'qtfluentwidgets/components/navigation/breadcrumb.cpp',
    'qtfluentwidgets/components/navigation/navigation_bar.cpp',
    'qtfluentwidgets/components/navigation/navigation_interface.cpp',
    'qtfluentwidgets/components/navigation/navigation_panel.cpp',
    'qtfluentwidgets/components/navigation/navigation_widget.cpp',
    'qtfluentwidgets/components/navigation/pivot.cpp',
    'qtfluentwidgets/components/navigation/segmented_widget.cpp',
    'qtfluentwidgets/components/navigation/top_navigation_interface.cpp',
    'qtfluentwidgets/components/navigation/top_navigation_panel.cpp',
    'qtfluentwidgets/components/settings/custom_color_setting_card.cpp',
    'qtfluentwidgets/components/settings/expand_setting_card.cpp',
    'qtfluentwidgets/components/settings/folder_list_setting_card.cpp',
    'qtfluentwidgets/components/settings/options_setting_card.cpp',
    'qtfluentwidgets/components/settings/setting_card.cpp',
    'qtfluentwidgets/components/settings/setting_card_group.cpp',
    'qtfluentwidgets/components/widgets/acrylic_label.cpp',
    'qtfluentwidgets/components/widgets/button.cpp',
    'qtfluentwidgets/components/widgets/card_widget.cpp',
    'qtfluentwidgets/components/widgets/check_box.cpp',
    'qtfluentwidgets/components/widgets/combo_box.cpp',
    'qtfluentwidgets/components/widgets/command_bar.cpp',
    'qtfluentwidgets/components/widgets/cycle_list_widget.cpp',
    'qtfluentwidgets/components/widgets/flip_view.cpp',
    'qtfluentwidgets/components/widgets/flyout.cpp',
    'qtfluentwidgets/components/widgets/frameless_window.cpp',
    'qtfluentwidgets/components/widgets/icon_widget.cpp',
    'qtfluentwidgets/components/widgets/info_badge.cpp',
    'qtfluentwidgets/components/widgets/info_bar.cpp',
    'qtfluentwidgets/components/widgets/label.cpp',
    'qtfluentwidgets/components/widgets/line_edit.cpp',
    'qtfluentwidgets/components/widgets/list_view.cpp',
    'qtfluentwidgets/components/widgets/menu.cpp',
    'qtfluentwidgets/components/widgets/model_combo_box.cpp',
    'qtfluentwidgets/components/widgets/pips_pager.cpp',
    'qtfluentwidgets/components/widgets/progress_bar.cpp',
    'qtfluentwidgets/components/widgets/progress_ring.cpp',
    'qtfluentwidgets/components/widgets/scroll_area.cpp',
    'qtfluentwidgets/components/widgets/scroll_bar.cpp',
    'qtfluentwidgets/components/widgets/separator.cpp',
    'qtfluentwidgets/components/widgets/slider.cpp',
    'qtfluentwidgets/components/widgets/spin_box.cpp',
    'qtfluentwidgets/components/widgets/stacked_widget.cpp',
    'qtfluentwidgets/components/widgets/state_tool_tip.cpp',
    'qtfluentwidgets/components/widgets/switch_button.cpp',
    'qtfluentwidgets/components/widgets/tab_view.cpp',
    'qtfluentwidgets/components/widgets/table_view.cpp',
    'qtfluentwidgets/components/widgets/teaching_tip.cpp',
    'qtfluentwidgets/components/widgets/tool_tip.cpp',
    'qtfluentwidgets/components/widgets/tree_view.cpp',
    'qtfluentwidgets/components/window/title_bar.cpp',
    'qtfluentwidgets/window/fluent_window.cpp',
    'qtfluentwidgets/window/stacked_widget.cpp',
)

qfw_moc_headers = files(
    'qtfluentwidgets/common/animation.h',
    'qtfluentwidgets/common/config.h',
    'qtfluentwidgets/common/icon.h',
    'qtfluentwidgets/common/router.h',
    'qtfluentwidgets/common/smooth_scroll.h',
    'qtfluentwidgets/common/style_sheet.h',
    'qtfluentwidgets/common/theme_listener.h',
    'qtfluentwidgets/components/date_time/calendar_picker.h',
    'qtfluentwidgets/components/date_time/calendar_view.h',
    'qtfluentwidgets/components/date_time/date_picker.h',
    'qtfluentwidgets/components/date_time/fast_calendar_picker.h',
    'qtfluentwidgets/components/date_time/fast_calendar_view.h',
    'qtfluentwidgets/components/date_time/picker_base.h',
    'qtfluentwidgets/components/date_time/time_picker.h',
    'qtfluentwidgets/components/dialog_box/color_dialog.h',
    'qtfluentwidgets/components/dialog_box/dialog.h',
    'qtfluentwidgets/components/dialog_box/folder_list_dialog.h',
    'qtfluentwidgets/components/dialog_box/mask_dialog_base.h',
    'qtfluentwidgets/components/dialog_box/message_box_base.h',
    'qtfluentwidgets/components/dialog_box/message_dialog.h',
    'qtfluentwidgets/components/layout/expand_layout.h',
    'qtfluentwidgets/components/layout/flow_layout.h',
    'qtfluentwidgets/components/layout/v_box_layout.h',
    'qtfluentwidgets/components/navigation/breadcrumb.h',
    'qtfluentwidgets/components/navigation/navigation_bar.h',
    'qtfluentwidgets/components/navigation/navigation_interface.h',
    'qtfluentwidgets/components/navigation/navigation_panel.h',
    'qtfluentwidgets/components/navigation/navigation_widget.h',
    'qtfluentwidgets/components/navigation/pivot.h',
    'qtfluentwidgets/components/navigation/segmented_widget.h',
    'qtfluentwidgets/components/navigation/top_navigation_interface.h',
    'qtfluentwidgets/components/navigation/top_navigation_panel.h',
    'qtfluentwidgets/components/settings/custom_color_setting_card.h',
    'qtfluentwidgets/components/settings/expand_setting_card.h',
    'qtfluentwidgets/components/settings/folder_list_setting_card.h',
    'qtfluentwidgets/components/settings/options_setting_card.h',
    'qtfluentwidgets/components/settings/setting_card.h',
    'qtfluentwidgets/components/settings/setting_card_group.h',
    'qtfluentwidgets/components/widgets/acrylic_label.h',
    'qtfluentwidgets/components/widgets/button.h',
    'qtfluentwidgets/components/widgets/card_widget.h',
    'qtfluentwidgets/components/widgets/check_box.h',
    'qtfluentwidgets/components/widgets/combo_box.h',
    'qtfluentwidgets/components/widgets/command_bar.h',
    'qtfluentwidgets/components/widgets/cycle_list_widget.h',
    'qtfluentwidgets/components/widgets/flip_view.h',
    'qtfluentwidgets/components/widgets/flyout.h',
    'qtfluentwidgets/components/widgets/frameless_window.h',
    'qtfluentwidgets/components/widgets/icon_widget.h',
    'qtfluentwidgets/components/widgets/info_badge.h',
    'qtfluentwidgets/components/widgets/info_bar.h',
    'qtfluentwidgets/components/widgets/label.h',
    'qtfluentwidgets/components/widgets/line_edit.h',
    'qtfluentwidgets/components/widgets/list_view.h',
    'qtfluentwidgets/components/widgets/menu.h',
    'qtfluentwidgets/components/widgets/model_combo_box.h',
    'qtfluentwidgets/components/widgets/pips_pager.h',
    'qtfluentwidgets/components/widgets/progress_bar.h',
    'qtfluentwidgets/components/widgets/progress_ring.h',
    'qtfluentwidgets/components/widgets/scroll_area.h',
    'qtfluentwidgets/components/widgets/scroll_bar.h',
    'qtfluentwidgets/components/widgets/separator.h',
    'qtfluentwidgets/components/widgets/slider.h',
    'qtfluentwidgets/components/widgets/spin_box.h',
    'qtfluentwidgets/components/widgets/stacked_widget.h',
    'qtfluentwidgets/components/widgets/state_tool_tip.h',
    'qtfluentwidgets/components/widgets/switch_button.h',
    'qtfluentwidgets/components/widgets/tab_view.h',
    'qtfluentwidgets/components/widgets/table_view.h',
    'qtfluentwidgets/components/widgets/teaching_tip.h',
    'qtfluentwidgets/components/widgets/tool_tip.h',
    'qtfluentwidgets/components/widgets/tree_view.h',
    'qtfluentwidgets/components/window/mac_frameless_window.h',
    'qtfluentwidgets/components/window/title_bar.h',
    'qtfluentwidgets/window/fluent_window.h',
)

qfw_extra_dependencies = []
if host_machine.system() == 'darwin'
    add_languages('objcpp')
    qfw_sources += files(
        'qtfluentwidgets/components/window/linux_frameless_window_stub.cpp',
        'qtfluentwidgets/components/window/mac_frameless_window.mm',
    )
    qfw_extra_dependencies += dependency('appleframeworks', modules: ['Cocoa'])
else
    qfw_sources += files(
        'qtfluentwidgets/components/window/mac_frameless_window.cpp',
    )
    if host_machine.system() == 'linux'
        qfw_sources += files(
            'qtfluentwidgets/components/window/linux_frameless_window.cpp',
        )
        qfw_moc_headers += files(
            'qtfluentwidgets/components/window/linux_frameless_window.h',
        )
    else
        qfw_sources += files(
            'qtfluentwidgets/components/window/linux_frameless_window_stub.cpp',
        )
    endif
endif

if host_machine.system() == 'windows'
    qfw_sources += files(
        'qtfluentwidgets/components/window/windows_frameless_window.cpp',
        'qtfluentwidgets/components/window/windows_window_effect.cpp',
    )
    qfw_moc_headers += files(
        'qtfluentwidgets/components/window/windows_frameless_window.h',
    )
    qfw_extra_dependencies += qfw_cpp.find_library('mpr', required: true)
endif

qfw_generated = qt6.preprocess(
    moc_headers: qfw_moc_headers,
    qresources: files('qtfluentwidgets/resources/resource.qrc'),
    include_directories: qfw_include,
    dependencies: qt6_dep,
)

# `components/widgets/stacked_widget.h` has the same basename. Generate the
# window variant in its own build directory so Ninja outputs stay unique.
subdir('qtfluentwidgets/window')
qfw_generated += qfw_window_stacked_widget_moc

qfw_library = static_library(
    'qtfluentwidgets',
    [qfw_sources, qfw_generated],
    include_directories: qfw_include,
    dependencies: [qt6_dep, qfw_extra_dependencies],
    cpp_args: qfw_cpp_args,
    override_options: ['warning_level=0', 'werror=false'],
)

qtfluentwidgets_dep = declare_dependency(
    link_with: qfw_library,
    include_directories: qfw_system_include,
    dependencies: [qt6_dep, qfw_extra_dependencies],
)

meson.override_dependency('qtfluentwidgets', qtfluentwidgets_dep)
