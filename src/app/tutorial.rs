//! 首次启动新手教程状态机。

use super::*;

impl AppController {
    pub(super) fn initialize_tutorial_state(&self) {
        let active = !self.app_prefs.tutorial_completed;
        self.ui.set_tutorial_active(active);
        self.ui.set_tutorial_step(if active { 1 } else { 0 });
    }

    pub(super) fn handle_tutorial_target_clicked(&mut self, step: i32) {
        if !self.tutorial_is_at(step) {
            return;
        }

        match step {
            1 => {
                self.ui.set_page(4);
                self.advance_tutorial_for_page(4);
            }
            2 => {
                self.ui.set_show_github_proxy_dialog(true);
                self.set_tutorial_step(3);
            }
            3 => {
                self.select_github_proxy_from_dialog(AUTO_SELECT_PROXY_URL.to_string());
            }
            4 => {
                self.ui.set_page(0);
                self.advance_tutorial_for_page(0);
            }
            5 => {
                if self.ui.get_has_target() && self.is_admin && !self.ui.get_busy() {
                    self.start_install();
                }
            }
            7 => {
                self.ui.set_page(2);
                self.advance_tutorial_for_page(2);
            }
            8 => {
                self.set_tutorial_step(9);
            }
            _ => {}
        }
    }

    pub(super) fn handle_tutorial_next_clicked(&mut self) {
        if self.tutorial_is_at(2) {
            self.set_tutorial_step(4);
        } else if self.tutorial_is_at(5) {
            self.set_tutorial_step(6);
        } else if self.tutorial_is_at(6) {
            self.set_tutorial_step(7);
        }
    }

    pub(super) fn handle_tutorial_skip_clicked(&mut self) {
        if self.tutorial_is_at(3) {
            self.ui.set_show_github_proxy_dialog(false);
        }
        self.complete_tutorial();
    }

    pub(super) fn handle_tutorial_finish_clicked(&mut self) {
        self.complete_tutorial();
    }

    pub(super) fn advance_tutorial_for_page(&mut self, page: i32) {
        if !self.ui.get_tutorial_active() {
            return;
        }

        match (self.ui.get_tutorial_step(), page) {
            (1, 4) => self.set_tutorial_step(2),
            (4, 0) => self.set_tutorial_step(5),
            (7, 2) => self.set_tutorial_step(8),
            _ => {}
        }
    }

    pub(super) fn advance_tutorial_after_proxy_auto_selected(&mut self) {
        if self.tutorial_is_at(3) {
            self.set_tutorial_step(4);
        }
    }

    pub(super) fn advance_tutorial_after_install_finished(&mut self) {
        if self.tutorial_is_at(5) {
            self.ui.set_install_progress_visible(false);
            self.set_tutorial_step(6);
        }
    }

    fn tutorial_is_at(&self, step: i32) -> bool {
        self.ui.get_tutorial_active() && self.ui.get_tutorial_step() == step
    }

    fn set_tutorial_step(&self, step: i32) {
        self.ui.set_tutorial_step(step);
    }

    fn complete_tutorial(&mut self) {
        self.ui.set_tutorial_active(false);
        self.ui.set_tutorial_step(0);
        self.app_prefs.tutorial_completed = true;
        self.save_app_prefs();
    }
}
