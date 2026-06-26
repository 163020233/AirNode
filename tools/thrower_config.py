#!/usr/bin/env python3
"""
AirNode 抛投器配置工具 (上位机 - 极速 CSV 桥接版)
通过串口发送极简文本指令，在底层与单片机进行极速、免丢包的通信。
"""

import tkinter as tk
from tkinter import ttk, scrolledtext, messagebox
import serial
import serial.tools.list_ports
import json
import threading
import time

# ============================================================
#  配置
# ============================================================
BAUDRATE = 115200
TIMEOUT = 1.0
MSG_DELIMITER = b"\r\n\r\n"


# ============================================================
#  串口通信线程 (日志过滤与 CSV 应答协议桥接器)
# ============================================================
class Stm32Serial:
    def __init__(self, callback):
        self.ser = None
        self.running = False
        self.recv_thread = None
        self.cseq = 0
        self.callback = callback  # on_response(json), on_connected(), on_disconnected()
        self.buffer = b""

    def list_ports(self):
        return [p.device for p in serial.tools.list_ports.comports()]

    def connect(self, port):
        try:
            self.ser = serial.Serial(port, BAUDRATE, timeout=TIMEOUT)
            self.ser.reset_input_buffer()
            self.ser.reset_output_buffer()

            self.running = True
            self.recv_thread = threading.Thread(target=self._recv_loop, daemon=True)
            self.recv_thread.start()
            self.callback("on_connected", None)
            return True
        except Exception as e:
            self.callback("on_disconnected", str(e))
            return False

    def disconnect(self):
        if self.ser is None and not self.running:
            return
        self.running = False
        if self.ser:
            try:
                self.ser.close()
            except:
                pass
            self.ser = None
        self.callback("on_disconnected", "disconnected")

    def send(self, data):
        if not self.ser or not self.ser.is_open:
            return False
        try:
            self.ser.write(data + MSG_DELIMITER)
            return True
        except:
            self.disconnect()
            return False

    # 发送极简 CSV 文本
    def send_csv(self, cmd_str):
        self.cseq += 1
        raw = cmd_str.encode("utf-8")
        print(f"[PC Debug] 发送 CSV 指令 -> {cmd_str}")
        return self.send(raw)

    def _recv_loop(self):
        while self.running:
            try:
                if self.ser and self.ser.is_open:
                    data = self.ser.read(256)
                    if data:
                        self.buffer += data
                        if len(self.buffer) > 4096:
                            self.buffer = b""
                            continue

                        while True:
                            idx = self.buffer.find(MSG_DELIMITER)
                            if idx == -1:
                                break
                            pkt = self.buffer[:idx]
                            self.buffer = self.buffer[idx + len(MSG_DELIMITER):]
                            try:
                                pkt_str = pkt.decode("utf-8", errors="ignore").strip()
                                if not pkt_str:
                                    continue

                                start_idx = pkt_str.find("{")
                                end_idx = pkt_str.rfind("}")

                                # A. 如果包含 {}，依然按照标准 JSON 解析 (向下兼容)
                                if start_idx != -1 and end_idx != -1 and end_idx > start_idx:
                                    json_str = pkt_str[start_idx:end_idx + 1]
                                    obj = json.loads(json_str)
                                    self.callback("on_response", obj)
                                else:
                                    # B. 如果是以 R, W, T 开头的极简 CSV 应答，在底层透明封装为 JSON 字典，无缝兼容 UI 层
                                    if pkt_str.startswith(("R,", "W,", "T,")):
                                        parts = pkt_str.split(",")
                                        cmd_type = parts[0]
                                        obj = {}
                                        if cmd_type == 'W':  # W,ch,code
                                            obj = {
                                                "command": "config_write",
                                                "ch": int(parts[1]),
                                                "code": parts[2]
                                            }
                                        elif cmd_type == 'R':  # R,ch,code,closed,released
                                            obj = {
                                                "command": "config_read_response",
                                                "ch": int(parts[1]),
                                                "code": parts[2],
                                                "closed": int(parts[3]),
                                                "released": int(parts[4])
                                            }
                                        elif cmd_type == 'T':  # T,ch,code
                                            obj = {
                                                "command": "servo_trigger",
                                                "ch": int(parts[1]),
                                                "code": parts[2],
                                                "action": "release" if parts[1] == "1" else "close"
                                            }
                                        self.callback("on_response", obj)
                                    else:
                                        # C. 纯系统文本日志，直接打印
                                        for line in pkt_str.split('\r\n'):
                                            if line.strip():
                                                print(f"[STM32 Log] {line.strip()}")
                            except Exception as e:
                                pass
                    else:
                        time.sleep(0.02)
                else:
                    break
            except:
                if self.running:
                    self.callback("on_disconnected", "connection lost")
                break

    def is_connected(self):
        return self.ser is not None and self.ser.is_open


# ============================================================
#  UI 界面显示层 (保持不变，无缝对接 CSV 桥接层)
# ============================================================
class App:
    def __init__(self, root):
        self.root = root
        self.root.title("AirNode 抛投器配置工具")
        self.root.geometry("720x780")
        self.root.resizable(False, False)

        self.stm32 = Stm32Serial(self._on_event)

        # ---------- 连接区 ----------
        frame_top = ttk.LabelFrame(root, text="串口连接", padding=10)
        frame_top.pack(fill="x", padx=10, pady=5)

        ttk.Label(frame_top, text="端口:").grid(row=0, column=0, sticky="w")
        self.port_var = tk.StringVar()
        self.port_combo = ttk.Combobox(frame_top, textvariable=self.port_var, width=25, state="readonly")
        self.port_combo.grid(row=0, column=1, padx=5)
        self.btn_refresh = ttk.Button(frame_top, text="刷新", command=self._refresh_ports)
        self.btn_refresh.grid(row=0, column=2, padx=5)
        self.btn_connect = ttk.Button(frame_top, text="连接", command=self._toggle_connect)
        self.btn_connect.grid(row=0, column=3, padx=5)
        self.lbl_status = ttk.Label(frame_top, text="未连接")
        self.lbl_status.grid(row=0, column=4, padx=10)

        # ---------- 通道区 ----------
        self.channels = []
        for ch in range(2):
            frame_ch = ttk.LabelFrame(root, text=f"通道 {ch}", padding=10)
            frame_ch.pack(fill="x", padx=10, pady=5)

            self.channels.append({
                "closed": tk.StringVar(value="--"),
                "released": tk.StringVar(value="--"),
                "current_pwm": tk.StringVar(value="--"),
                "state": tk.StringVar(value="--"),
                "closed_entry": tk.StringVar(value="1500"),
                "released_entry": tk.StringVar(value="2000"),
            })

            ttk.Label(frame_ch, text="当前实时状态:", font=("", 10, "bold")).grid(row=0, column=0, sticky="w",
                                                                                  columnspan=3)
            self._add_row(frame_ch, 1, "当前 PWM 值：", "current_pwm")
            self._add_row(frame_ch, 2, "当前状态：", "state")

            ttk.Label(frame_ch, text="已保存配置:", font=("", 10, "bold")).grid(row=3, column=0, sticky="w",
                                                                                columnspan=3, pady=(8, 0))
            self._add_row(frame_ch, 4, "闭合 PWM：", "closed")
            self._add_row(frame_ch, 5, "投掷 PWM：", "released")

            ttk.Label(frame_ch, text="新配置:", font=("", 10, "bold")).grid(row=6, column=0, sticky="w", columnspan=3,
                                                                            pady=(8, 0))
            self._add_entry_row(frame_ch, 7, "闭合 PWM：", "closed_entry")
            self._add_entry_row(frame_ch, 8, "投掷 PWM：", "released_entry")

            # 操作按钮
            btn_frame = ttk.Frame(frame_ch)
            btn_frame.grid(row=9, column=0, columnspan=3, pady=5)
            ttk.Button(btn_frame, text="写入配置",
                       command=lambda c=ch: self._write_config(c)).pack(side="left", padx=5)
            ttk.Button(btn_frame, text="读取配置",
                       command=lambda c=ch: self._read_config(c)).pack(side="left", padx=5)
            ttk.Button(btn_frame, text="闭合",
                       command=lambda c=ch: self._trigger(c, "close")).pack(side="left", padx=5)
            ttk.Button(btn_frame, text="投掷",
                       command=lambda c=ch: self._trigger(c, "release")).pack(side="left", padx=5)

        # ---------- 日志区 ----------
        frame_log = ttk.LabelFrame(root, text="日志", padding=5)
        frame_log.pack(fill="both", expand=True, padx=10, pady=5)
        self.log = scrolledtext.ScrolledText(frame_log, height=12, state="disabled", font=("Consolas", 9))
        self.log.pack(fill="both", expand=True)

        self._refresh_ports()

    def _add_row(self, parent, row, label, key):
        ttk.Label(parent, text=label).grid(row=row, column=0, sticky="w", padx=20)
        lbl = ttk.Label(parent, textvariable=self.channels[-1][key])
        lbl.grid(row=row, column=1, sticky="w")

    def _add_entry_row(self, parent, row, label, key):
        ttk.Label(parent, text=label).grid(row=row, column=0, sticky="w", padx=20)
        entry = ttk.Entry(parent, textvariable=self.channels[-1][key], width=12)
        entry.grid(row=row, column=1, sticky="w")

    def _on_event(self, event, data):
        if event == "on_connected":
            self.root.after(0, self._on_connected)
        elif event == "on_disconnected":
            self.root.after(0, self._on_disconnected, data)
        elif event == "on_response":
            self.root.after(0, self._on_response, data)

    def _on_connected(self):
        self.btn_connect.config(text="断开")
        self.lbl_status.config(text="已连接", foreground="green")
        self._log("串口已连接，正在等待设备初始化...")

        # 异步开机读取，完美避开冲突
        self.root.after(1500, lambda: self._read_config(0))
        self.root.after(1700, lambda: self._read_config(1))

    def _on_disconnected(self, reason):
        self.stm32.disconnect()
        self.btn_connect.config(text="连接")
        self.lbl_status.config(text="未连接", foreground="red")
        self._log(f"断开: {reason}")

    def _on_response(self, resp):
        print(f"[PC Debug] 收到应答 <- {resp}")

        cmd = resp.get("command", "")
        code = resp.get("code", "")
        ch = resp.get("ch", 0)
        msg = resp.get("msg", "")

        if cmd == "config_read_response":
            closed_val = resp.get("closed", "--")
            released_val = resp.get("released", "--")
            if ch == 0 or ch == 1:
                self.channels[ch]["closed"].set(closed_val)
                self.channels[ch]["released"].set(released_val)
                self.channels[ch]["closed_entry"].set(closed_val)
                self.channels[ch]["released_entry"].set(released_val)
            self._log(f"CH{ch} 配置已读取: closed={closed_val}, released={released_val}")

        elif cmd == "config_write":
            if "code" not in resp:
                return
            if code == "200":
                self._log(f"CH{ch} 配置写入成功")
            else:
                self._log(f"CH{ch} 配置写入失败: {msg}")

        elif cmd == "servo_trigger":
            if "code" not in resp:
                return
            action = resp.get("action", "release" if ch == 1 else "close")
            if code == "200":
                self._log(f"CH{ch} {action} 执行成功")
            else:
                self._log(f"CH{ch} {action} 执行失败: {msg}")

        elif cmd == "servo_status":
            self.channels[ch]["current_pwm"].set(resp.get("pwm", "--"))
            self.channels[ch]["state"].set(resp.get("state", "--"))

    def _toggle_connect(self):
        if self.stm32.is_connected():
            self.stm32.disconnect()
        else:
            port = self.port_var.get()
            if not port:
                messagebox.showwarning("提示", "请选择串口")
                return
            self.btn_connect.config(text="连接中...", state="disabled")
            self.root.update()
            self.stm32.connect(port)
            self.btn_connect.config(state="normal")

    def _refresh_ports(self):
        ports = self.stm32.list_ports()
        self.port_combo["values"] = ports
        if ports and not self.port_var.get():
            self.port_var.set(ports[0])

    def _write_config(self, ch):
        if not self.stm32.is_connected():
            messagebox.showwarning("提示", "请先连接串口")
            return
        try:
            closed = int(self.channels[ch]["closed_entry"].get())
            released = int(self.channels[ch]["released_entry"].get())
        except ValueError:
            messagebox.showwarning("提示", "请输入有效数字")
            return

        if closed < 500 or closed > 2500 or released < 500 or released > 2500:
            messagebox.showwarning("提示", "标定脉宽超出安全限幅 (推荐 500~2500 微秒)")
            return

        # 写入命令改为极简 CSV： W,ch,closed,released
        self.stm32.send_csv(f"W,{ch},{closed},{released}")
        self._log(f"写入 CH{ch}: closed={closed}, released={released}")

    def _read_config(self, ch):
        if not self.stm32.is_connected():
            return
        # 读取命令改为极简 CSV： R,ch
        self.stm32.send_csv(f"R,{ch}")
        print(f"[PC Debug] 触发读取 CH{ch} 配置指令")

    def _trigger(self, ch, action):
        if not self.stm32.is_connected():
            messagebox.showwarning("提示", "请先连接串口")
            return
        action_val = 1 if action == "release" else 0
        # 触发指令改为极简 CSV： T,ch,action_val
        self.stm32.send_csv(f"T,{ch},{action_val}")
        self._log(f"触发 CH{ch}: {action}")

    def _log(self, msg):
        self.log.config(state="normal")
        t = time.strftime("%H:%M:%S")
        self.log.insert("end", f"[{t}] {msg}\n")
        self.log.see("end")
        self.log.config(state="disabled")


if __name__ == "__main__":
    root = tk.Tk()
    app = App(root)
    root.mainloop()