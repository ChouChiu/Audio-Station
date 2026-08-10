from __future__ import annotations

from qfluentwidgets import ComboBox, MenuAnimationType
from qfluentwidgets.components.widgets.combo_box import ComboBoxMenu
from qfluentwidgets.components.widgets.menu import MenuAnimationManager


class SmoothComboBoxMenu(ComboBoxMenu):
    """Combo menu positioned by Fluent without a platform-dependent animation."""

    def exec(
        self,
        pos,
        ani: bool = True,
        aniType: MenuAnimationType = MenuAnimationType.DROP_DOWN,
    ):
        # QFluentWidgets' slide animation looks distorted for long menus, while
        # its fade variant repeatedly calls setWindowOpacity(), which is not
        # supported by every Qt platform plugin.  Use the requested direction
        # only to calculate the final position, then explicitly select Fluent's
        # no-animation manager for display.
        self.view.adjustSize(pos, aniType)
        self.adjustSize()
        position_manager = MenuAnimationManager.make(self, aniType)
        end_position = position_manager._endPosition(pos)
        self.aniManager = MenuAnimationManager.make(self, MenuAnimationType.NONE)
        self.move(end_position)
        self.clearMask()
        self.show()
        return None


class SmoothComboBox(ComboBox):
    """Project combo box with a stable, immediate popup."""

    def _createComboMenu(self):
        return SmoothComboBoxMenu(self)
