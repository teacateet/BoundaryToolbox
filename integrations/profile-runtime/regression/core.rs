use super::{CREATE_NO_WINDOW, INSTALLER_FOLDER_NAME, LaunchFiles};

#[allow(dead_code)]
#[path = "../../../src/core/local_profile.rs"]
pub(crate) mod local_profile;
#[allow(dead_code)]
#[path = "../../../src/core/util.rs"]
mod util;

pub(super) fn running_managed_pid_at(home: &std::path::Path, files: &LaunchFiles, port: u16)
    -> anyhow::Result<Option<u32>> {
    local_profile::running_managed_pid_at(home, files, port)
}
