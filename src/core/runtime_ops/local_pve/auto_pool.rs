use super::*;
use client_join::{Bridge, owners, read_line};
use local_pve::spawn_room;
use serde_json::Value;
use std::collections::HashSet;
use std::net::TcpStream;
use std::sync::{
    Arc,
    atomic::{AtomicBool, Ordering},
};
use windows::Win32::System::JobObjects::*;

#[derive(Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
struct Prepare {
    v: u8,
    op: String,
    run_id: String,
    nonce: String,
    request_id: String,
    mode: String,
    map: Option<String>,
    client_port: u16,
    rpc_port: u16,
}

impl Prepare {
    fn difficulty(&self) -> Result<usize> {
        ["Rush_PVE_Easy", "Rush_PVE_Normal", "Rush_PVE_Hard"]
            .iter()
            .position(|&m| m == self.mode)
            .context("无效的本机难度")
    }
    fn verify_rpc(&self, game: u32, node: u32) -> Result<()> {
        owners(&[
            (self.client_port, self.rpc_port, game),
            (self.rpc_port, self.client_port, node),
        ])
    }
}

// 控制连接的 FIN 即撤销。prepare 后没有下一条入站命令。
fn connected(s: &TcpStream) -> bool {
    matches!(s.peek(&mut [0; 1]), Err(e) if e.kind() == ErrorKind::WouldBlock)
}

fn room_job(child: &Child) -> Result<OwnedHandle> {
    let raw = unsafe { CreateJobObjectW(None, None)? };
    let job = unsafe { OwnedHandle::from_raw_handle(raw.0) };
    let mut limits = JOBOBJECT_EXTENDED_LIMIT_INFORMATION::default();
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    unsafe {
        SetInformationJobObject(
            raw,
            JobObjectExtendedLimitInformation,
            (&raw const limits).cast(),
            size_of_val(&limits) as u32,
        )?;
        AssignProcessToJobObject(raw, HANDLE(child.as_raw_handle()))?;
    }
    Ok(job)
}

struct Active {
    _job: OwnedHandle,
    room: Room,
    plan: local_pve::Plan,
    cfg: Selection,
    round: local_pve::RoundLog,
    spawn_proof: Option<Value>,
}

impl Active {
    fn retire(self) -> Result<()> {
        drop(self._job);
        let result = unsafe {
            windows::Win32::System::Threading::WaitForSingleObject(self.room.game.raw(), 3000)
        };
        ensure!(result == WAIT_OBJECT_0, "上一场房间仍在退出");
        Ok(())
    }

    fn check(&self) -> Result<()> {
        require_running(
            [
                ("对局启动器", self.room.wrapper.raw()),
                ("本机对局", self.room.game.raw()),
            ],
            Duration::ZERO,
        )?;
        ensure!(
            self.cfg == Selection::read_from(&self.plan.dir().join("serverconfig.json"))?,
            "房间配置已改变"
        );
        ensure!(
            Room::socket_owner(self.cfg.port)? == Some(self.room.game.id),
            "房间端口已换代"
        );
        Ok(())
    }
}

fn start_room(files: &LaunchFiles, plan: local_pve::Plan, cancel: &AtomicBool, grant: Option<spawn_room::Grant>) -> Result<Active> {
    let cfg = Selection::read_from(&plan.dir().join("serverconfig.json"))?;
    ensure!(cfg.port == plan.port(), "房间端口不符");
    let mut job = None;
    let mut pins = None;
    let spawn_proof = grant.as_ref().map(spawn_room::Grant::proof);
    let ready = plan.launch_while(
        files,
        move |cmd| { if let Some(g) = grant { g.attach(cmd); } },
        || {
            ensure!(!cancel.load(Ordering::Acquire), "选择已撤销");
            Ok(())
        },
        |child| {
            job = Some(room_job(child)?);
            Ok(())
        },
        || {
            let id = Room::socket_owner(cfg.port)?.context("本机房间未就绪")?;
            pins = Some(Room::pin_launcher(files, id, plan.dir(), plan.launcher(files))?);
            Ok(())
        },
    )?;
    let room = pins.context("未取得房间进程")?;
    ensure!(
        room.game.id == ready.pid && ready.map == cfg.map() && ready.endpoint.port() == cfg.port,
        "就绪房间已改变"
    );
    let active = Active {
        _job: job.context("缺少房间作业")?,
        room,
        plan,
        cfg,
        round: ready.round,
        spawn_proof,
    };
    active.check()?;
    Ok(active)
}

struct Pending {
    cancel: Arc<AtomicBool>,
    worker: Option<thread::JoinHandle<Result<Active>>>,
}

impl Drop for Pending {
    fn drop(&mut self) {
        self.cancel.store(true, Ordering::Release);
        if let Some(worker) = self.worker.take() {
            let _ = worker.join();
        }
    }
}

struct Choice {
    request: Prepare,
    socket: TcpStream,
    pending: Pending,
    ready: Option<(Active, Bridge)>,
}

struct Pool {
    slot: Slot,
    profile: Deployment,
    control: Bridge,
    adv: Value,
    turns: [u64; 3],
    seen: HashSet<Uuid>,
    journal: JoinLog,
    pipe: String,
    playing: Option<(Active, String)>,
    spawn_owner: fn() -> Option<String>,
    spawn_signer: Option<[u8;32]>,
}

impl Pool {
    fn open(home: &Path, profile: Deployment) -> Result<Self> {
        let slot = Slot::claim(home, &profile.lease)?;
        let spawn_signer = local_profile::room_attestation::read(&profile.lease).ok();
        let control = Bridge::bind()?;
        let adv = control.advertisement()?;
        let turns = match OpenOptions::new()
            .read(true)
            .custom_flags(FILE_FLAG_OPEN_REPARSE_POINT.0)
            .open(slot.file.with_extension("turns"))
        {
            Ok(f) => {
                ensure!(
                    ordinary(&f.metadata()?) && f.metadata()?.len() <= 128,
                    "轮换计数文件无效"
                );
                serde_json::from_reader(f.take(129))?
            }
            Err(e) if e.kind() == ErrorKind::NotFound => [0; 3],
            Err(e) => return Err(e.into()),
        };
        Ok(Self {
            slot,
            profile,
            control,
            adv,
            turns,
            seen: HashSet::new(),
            journal: JoinLog {
                path: home.join("pve-join.log"),
                next_failure: Instant::now(),
            },
            pipe: client_join::pipe_name(),
            playing: None,
            spawn_owner: spawn_room::active_owner,
            spawn_signer,
        })
    }

    fn count(&mut self, idx: usize) -> Result<()> {
        let mut next = self.turns;
        next[idx] = next[idx].checked_add(1).context("轮换计数溢出")?;
        let file = self.slot.file.with_extension("turns");
        let temp = file.with_extension(format!("{}.tmp", Uuid::new_v4()));
        let mut out = File::create_new(&temp)?;
        let result = (|| -> Result<()> {
            serde_json::to_writer(&mut out, &next)?;
            out.sync_all()?;
            drop(out);
            fs::rename(&temp, file)?;
            Ok(())
        })();
        if result.is_err() {
            let _ = fs::remove_file(&temp);
        }
        result?;
        self.turns = next;
        Ok(())
    }

    fn publish(&mut self, choice: Option<&Choice>) -> Result<()> {
        let mut value = json!({"v":1,"autoPool":true,"nextMatch":1,"mode":"pve","offline":true,"runId":self.slot.run,
            "expiresAt":now_ms()?+TTL_MS,"controlIpc":self.adv});
        let owner = (self.spawn_owner)();
        let eligible = |room: &Active| owner.is_some() && room.spawn_proof.as_ref()
            .is_some_and(|p| p["owner"].as_str() == owner.as_deref());
        if let Some((room, choice_id)) = &mut self.playing && room.check().is_ok() {
            if eligible(room) { value["spawnChoice"] = json!(choice_id); }
            if room.round.started() { value["startedChoice"] = json!(choice_id); }
        }
        if let Some(c) = choice {
            // 此选择已通过大厅连接与归属验证。加载完成后的地址单独发布。
            value["queuedChoice"] = json!(c.request.request_id);
        }
        if let Some(c) = choice
            && let Some((room, bridge)) = &c.ready
        {
            room.check()?;
            if eligible(room) { value["spawnChoice"] = json!(c.request.request_id); }
            let cfg = &room.cfg;
            value.as_object_mut().unwrap().extend(json!({"choiceId":c.request.request_id,
                "difficulty":cfg.difficulty(),"map":cfg.map(),"port":cfg.port,"gameClass":cfg.class()?,
                "joinIpc":bridge.advertisement()?}).as_object().unwrap().clone());
        }
        self.slot.replace(serde_json::to_vec(&value)?)
    }

    fn accept(
        &mut self,
        game: u32,
        files: &LaunchFiles,
        verify: &impl Fn() -> Result<Option<u32>>,
        stage: fn(&LaunchFiles, &str, u64) -> Result<(LaunchFiles, local_pve::Plan)>,
    ) -> Result<Option<Choice>> {
        let node = self.profile.process.id;
        let Some(mut socket) = self.control.accept_node(node)? else {
            return Ok(None);
        };
        let request: Prepare = read_line(&mut socket)?;
        let id = Uuid::parse_str(&request.request_id)?;
        let idx = request.difficulty()?;
        ensure!(
            request.v == 1
                && request.op == "prepare"
                && request.run_id == self.slot.run
                && request.nonce == self.adv["nonce"]
                && id.get_version_num() == 4
                && id.to_string() == request.request_id,
            "选择或控制凭证无效"
        );
        request.verify_rpc(game, node)?;
        ensure!(verify()? == Some(node), "档案部署已切换");
        socket.set_nonblocking(true)?;
        ensure!(connected(&socket), "选择已取消");
        ensure!(
            self.seen.len() < 4096 && self.seen.insert(id),
            "此选择已处理"
        );
        let files = files.clone();
        let turn = match &request.map {
            Some(map) => local_pve::auto_room::selected_slot(&files, &request.mode, map)?,
            None => self.turns[idx],
        };
        // 只有同一大厅的合法新选择才替换已交接的房间。
        if let Some((previous, _)) = self.playing.take() {
            previous.retire()?;
        }
        let mode = request.mode.clone();
        let grant = (self.spawn_owner)().zip(self.spawn_signer.as_ref())
            .map(|(owner, signer)| spawn_room::Grant::issue(id, owner, &self.slot.run, signer)).transpose()?;
        let selected = request.map.clone();
        let cancel = Arc::new(AtomicBool::new(false));
        let guard = cancel.clone();
        let worker = thread::Builder::new()
            .name("pve-room-start".into())
            .spawn(move || {
                let (files, plan) = stage(&files, &mode, turn)?;
                let active = start_room(&files, plan, &guard, grant)?;
                ensure!(selected.as_ref().is_none_or(|map| map == active.cfg.map()), "准备期间所选地图已变化");
                Ok(active)
            })?;
        Ok(Some(Choice {
            request,
            socket,
            pending: Pending {
                cancel,
                worker: Some(worker),
            },
            ready: None,
        }))
    }

    fn tick(
        &mut self,
        c: &mut Choice,
        child: &mut Child,
        verify: &impl Fn() -> Result<Option<u32>>,
    ) -> Result<bool> {
        ensure!(connected(&c.socket), "选择已取消");
        let node = self.profile.process.id;
        if c.pending.worker.as_ref().is_some_and(|w| w.is_finished()) {
            let room = c
                .pending
                .worker
                .take()
                .unwrap()
                .join()
                .map_err(|_| anyhow::anyhow!("房间工作线程中断"))??;
            c.request.verify_rpc(child.id(), node)?;
            let mut bridge = Bridge::bind()?;
            bridge.bind_rpc(c.request.client_port, c.request.rpc_port);
            ensure!(
                verify()? == Some(node) && connected(&c.socket),
                "大厅选择已换代"
            );
            let mut prepared = json!({"op":"prepared","requestId":c.request.request_id});
            if let Some(proof) = &room.spawn_proof
                && proof["owner"].as_str() == (self.spawn_owner)().as_deref() {
                prepared["spawn"] = proof.clone();
            }
            c.ready = Some((room, bridge));
            self.publish(Some(c))?;
            let msg = format!("{prepared}\n");
            ensure!(
                c.socket.write(msg.as_bytes())? == msg.len(),
                "未能确认已准备房间"
            );
            self.journal.record("room-prepared");
        }
        let Some((room, bridge)) = &mut c.ready else {
            return Ok(false);
        };
        let result = bridge.dispatch(
            &self.slot.run,
            &self.pipe,
            child,
            node,
            room.cfg.port,
            || {
                ensure!(
                    verify()? == Some(node) && connected(&c.socket),
                    "选择已撤销"
                );
                room.check()?;
                let bytes = self.slot.contents()?.context("租约已撤销")?;
                ensure!(Some(&bytes) == self.slot.expected.as_ref(), "租约已换代");
                let receipt: Value = serde_json::from_slice(&bytes)?;
                ensure!(
                    receipt["expiresAt"].as_u64().unwrap_or(0) > now_ms()?
                        && receipt["choiceId"] == c.request.request_id,
                    "本次选择已过期"
                );
                ensure!(connected(&c.socket), "选择已撤销");
                Ok(())
            },
        );
        match result {
            Ok(true) => {
                self.playing = c.ready.take().map(|(active, _)| (active, c.request.request_id.clone()));
                self.journal.record("join_ack");
                self.count(c.request.difficulty()?)?;
                return Ok(true);
            }
            Err(_) => self.journal.record("join-failed"),
            _ => {}
        }
        Ok(false)
    }

    fn run(
        &mut self,
        mut child: Child,
        files: LaunchFiles,
        verify: impl Fn() -> Result<Option<u32>>,
        stage: fn(&LaunchFiles, &str, u64) -> Result<(LaunchFiles, local_pve::Plan)>,
    ) -> Result<()> {
        let result = self.serve(&mut child, &files, &verify, stage);
        if self.playing.is_some() {
            self.slot.withdraw();
            child.wait().context("等待本次原作大厅结束")?;
            drop(self.playing.take());
        }
        result
    }

    fn serve(
        &mut self,
        child: &mut Child,
        files: &LaunchFiles,
        verify: &impl Fn() -> Result<Option<u32>>,
        stage: fn(&LaunchFiles, &str, u64) -> Result<(LaunchFiles, local_pve::Plan)>,
    ) -> Result<()> {
        let mut choice = None;
        let mut renewal = Instant::now();
        let mut probe = Instant::now();
        let mut pipe_ready = false;
        loop {
            require_running(
                [
                    ("档案服务", self.profile.process.raw()),
                    ("原作大厅", HANDLE(child.as_raw_handle())),
                ],
                Duration::from_millis(50),
            )?;
            if !pipe_ready && Instant::now() >= probe {
                probe = Instant::now() + Duration::from_secs(1);
                pipe_ready = Bridge::probe(&self.pipe, child).is_ok();
                if pipe_ready {
                    self.journal.record("pipe-ready");
                }
            }
            if let Some(c) = &mut choice {
                match self.tick(c, child, verify) {
                    Ok(false) => (),
                    outcome => {
                        if outcome.is_err() {
                            self.journal.record("room-closed");
                        }
                        self.publish(None)?;
                        choice = None;
                    }
                }
            } else if pipe_ready {
                match self.accept(child.id(), files, verify, stage) {
                    Ok(next) => {
                        if let Some(c) = &next {
                            self.publish(Some(c))?;
                        }
                        choice = next;
                    }
                    Err(_) => self.journal.record("prepare-failed"),
                }
            }
            if Instant::now() >= renewal {
                ensure!(verify()? == Some(self.profile.process.id), "档案部署已切换");
                if let Some(c) = &choice {
                    if c.ready.as_ref().is_some_and(|(r, _)| r.check().is_err())
                        || c.request
                            .verify_rpc(child.id(), self.profile.process.id)
                            .is_err()
                    {
                        choice = None;
                    }
                }
                self.publish(choice.as_ref())?;
                renewal = Instant::now() + RENEW;
            }
        }
    }
}

pub(in crate::core::runtime_ops) fn launch_pool(
    home: &Path,
    files: &LaunchFiles,
    lobby: impl FnOnce(&str) -> Result<Child>,
    stopped: impl FnOnce(String) + Send + 'static,
) -> Result<()> {
    let profile = Deployment::verified(home, files)?;
    let mut pool = Pool::open(home, profile)?;
    pool.publish(None)?;
    let name = pool.pipe.clone();
    let home = home.to_owned();
    let files = files.clone();
    let (tx, rx) = mpsc::channel();
    let worker = thread::Builder::new()
        .name("pve-lobby".into())
        .spawn(move || {
            if let Ok(child) = rx.recv() {
                let result = pool.run(
                    child,
                    files.clone(),
                    || local_profile::running_managed_pid(&home, &files),
                    local_pve::auto_room::stage_managed,
                );
                drop(pool);
                if let Err(e) = result {
                    stopped(format!("{e:#}"));
                }
            }
        })?;
    match lobby(&name) {
        Ok(child) => tx
            .send(child)
            .map_err(|_| anyhow::anyhow!("大厅监控已退出")),
        Err(e) => {
            drop(tx);
            let _ = worker.join();
            Err(e)
        }
    }
}

#[cfg(test)]
#[path = "auto_pool_tests.rs"]
mod tests;
