import streamlit as st
import pandas as pd
import socket
import struct
import numpy as np
import altair as alt
import os
import time
from datetime import datetime

# --- STYLES & CONFIG ---
st.set_page_config(
    page_title="Hex-Pickup Oscilloscope",
    page_icon="🎸",
    layout="wide",
)

# Refined CSS for the "Instrument Cluster" look
st.markdown("""
<style>
/* Reduce top margin of the main area */
.main .block-container { 
    padding-top: 16px !important; 
    padding-bottom: 1rem; 
    max-width: 98%;
}

/* Main title styling */
h1 {
    font-size: 1.1rem !important;
    margin-bottom: 12px !important;
    padding-top: 0px !important;
}

/* Chart rows: 4px gap between each, no extra spacing */
[data-testid="stVegaLiteChart"] {
    margin: 0 0 4px 0 !important;
    padding: 0 !important;
    line-height: 0 !important;
    display: block !important;
}
[data-testid="stVegaLiteChart"] > div,
[data-testid="stVegaLiteChart"] canvas,
[data-testid="stVegaLiteChart"] svg {
    display: block !important;
    margin: 0 !important;
}

/* Replace spinning running indicator with LIVE badge */
[data-testid="stStatusWidget"] svg {
    display: none !important;
}
[data-testid="stStatusWidget"] > div:first-child {
    font-size: 0 !important;
    color: transparent !important;
}
[data-testid="stStatusWidget"] > div:first-child::before {
    content: "⬤ LIVE";
    color: #ff4b4b;
    font-weight: bold;
    font-size: 0.75rem;
    letter-spacing: 0.08em;
}
</style>
""", unsafe_allow_html=True)

# --- DIRECTORY SETUP ---
LOG_DIR = os.path.expanduser("~/logs/graphs")
if not os.path.exists(LOG_DIR):
    os.makedirs(LOG_DIR)

# --- SOCKET MANAGEMENT ---
@st.cache_resource
def get_udp_socket():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("0.0.0.0", 5005))
    sock.setblocking(False)
    return sock

# --- SIDEBAR: NAVIGATION & CONTROLS ---
st.sidebar.title("🎸 SignalAssistant")
app_mode = st.sidebar.selectbox("App Mode", ["Live Monitor", "Review Recordings"])

# --- SESSION STATE ---
if 'is_recording' not in st.session_state:
    st.session_state.is_recording = False
if 'recorded_data' not in st.session_state:
    st.session_state.recorded_data = []
if 'data_buffer' not in st.session_state:
    st.session_state.data_buffer = np.zeros((300, 6))

COLUMNS = [f"String {i+1}" for i in range(6)]
STRING_COLORS = ["#1f77b4", "#ff7f0e", "#2ca02c", "#d62728", "#9467bd", "#8c564b"]

def get_color(val):
    if val > 0.7: return "#ff4b4b" # Red
    if val > 0.3: return "#ffaa00" # Yellow
    return "#00ff00"               # Green

def save_recording(name, data_list):
    if not data_list:
        return
    df = pd.DataFrame(data_list, columns=['timestamp'] + COLUMNS)
    filename = f"{name}.parquet"
    full_path = os.path.join(LOG_DIR, filename)
    df.to_parquet(full_path)
    st.sidebar.success(f"Saved: {filename}")
    st.session_state.recorded_data = []

# --- MODE 1: LIVE MONITOR ---
if app_mode == "Live Monitor":
    st.title("Live 6-String Stacked Scope")
    
    # Sidebar
    st.sidebar.header("Recording Control")
    def toggle_recording():
        st.session_state.is_recording = not st.session_state.is_recording

    rec_label = "🛑 Stop Recording" if st.session_state.is_recording else "🔴 Start Recording"
    st.sidebar.button(rec_label, on_click=toggle_recording, use_container_width=True)
    recording_name = st.sidebar.text_input("Recording Name", value=datetime.now().strftime("session_%Y%m%d_%H%M%S"))

    st.sidebar.markdown("---")
    y_ceiling = st.sidebar.number_input("Y-Axis Max", min_value=0.1, value=3.0, step=0.5)
    history_length = st.sidebar.slider("History", 50, 1000, 300)
    needle_pos = st.sidebar.slider("Needle Position", 0.1, 1.0, 0.8)
    view_mode = st.sidebar.radio("Mode", ["Raw RMS", "Calibrated RMS"])

    # Local Calibration
    st.sidebar.subheader("Gains")
    gains = [st.sidebar.number_input(f"S{i+1}", value=1.0, step=0.1, key=f"g{i}") for i in range(6)]

    # --- UI LAYOUT: 6 bare chart slots ---
    chart_slots = [st.empty() for _ in range(6)]
    chart_slots = chart_slots[::-1]  # reverse: slot 0 = string 6 at top

    if st.session_state.data_buffer.shape[0] != history_length:
        st.session_state.data_buffer = np.zeros((history_length, 6))

    sock = get_udp_socket()
    last_ui_update = time.time()
    UI_UPDATE_INTERVAL = 1.0 / 30.0 

    while True:
        try:
            packets = []
            while True:
                try:
                    data, addr = sock.recvfrom(24)
                    packets.append(data)
                except (BlockingIOError, socket.error):
                    break 
            
            if not packets:
                time.sleep(0.01) 
                if not st.session_state.is_recording and len(st.session_state.recorded_data) > 0:
                    save_recording(recording_name, st.session_state.recorded_data)
                    st.rerun()
                continue
            
            latest_display_vals = []
            for data in packets:
                incoming = list(struct.unpack('ffffff', data))
                timestamp = time.time()
                display_vals = [v * (gains[i] if view_mode == "Calibrated RMS" else 1.0) for i, v in enumerate(incoming)]
                
                if st.session_state.is_recording:
                    st.session_state.recorded_data.append([timestamp] + display_vals)

                st.session_state.data_buffer = np.roll(st.session_state.data_buffer, -1, axis=0)
                st.session_state.data_buffer[-1] = display_vals
                latest_display_vals = display_vals 

            current_time = time.time()
            if current_time - last_ui_update >= UI_UPDATE_INTERVAL:
                last_ui_update = current_time
                
                needle_idx = int(history_length * needle_pos)
                for i in range(6):
                    val = latest_display_vals[i]
                    color = get_color(val)

                    df_single = pd.DataFrame({
                        'index': range(history_length),
                        'RMS': st.session_state.data_buffer[:, i]
                    })

                    line = alt.Chart(df_single).mark_line(
                        color=STRING_COLORS[i], strokeWidth=2
                    ).encode(
                        x=alt.X('index:Q', axis=None, scale=alt.Scale(nice=False)),
                        y=alt.Y('RMS:Q',
                            scale=alt.Scale(domain=[0, y_ceiling], nice=False, zero=True, padding=0),
                            axis=alt.Axis(title=None, labels=True, ticks=True, domain=False,
                                          grid=True, gridColor='#444444', gridOpacity=0.4,
                                          labelColor='#aaaaaa', tickColor='#aaaaaa',
                                          labelFontSize=9, tickCount=3, labelPadding=10)
                        )
                    )

                    needle = alt.Chart(pd.DataFrame({'x': [needle_idx]})).mark_rule(
                        color='white', strokeDash=[4, 4], opacity=0.3
                    ).encode(x='x:Q')

                    label_df = pd.DataFrame([{'x': 2, 'y': y_ceiling * 0.85, 'text': f'STR {i+1}  {val:.3f}'}])
                    label = alt.Chart(label_df).mark_text(
                        align='left', baseline='top', fontSize=10,
                        fontWeight='bold', color=STRING_COLORS[i]
                    ).encode(x=alt.X('x:Q'), y=alt.Y('y:Q'), text='text:N')

                    chart = (line + needle + label).properties(
                        height=140, padding={'top': 0, 'bottom': 0, 'left': 0, 'right': 0}
                    ).configure_view(
                        strokeOpacity=0, fill='#181B1F'
                    ).configure(
                        background='#181B1F', padding=0
                    )
                    chart_slots[i].altair_chart(chart, use_container_width=True)

        except Exception as e:
            st.error(f"Stream Error: {e}")
            break

# --- MODE 2: REVIEW RECORDINGS ---
elif app_mode == "Review Recordings":
    st.title("Review Saved Recordings")
    files = [f for f in os.listdir(LOG_DIR) if f.endswith(".parquet")]
    if not files:
        st.info(f"No recordings found in {LOG_DIR}")
    else:
        selected_file = st.sidebar.selectbox("Select Recording", sorted(files, reverse=True))
        if selected_file:
            file_path = os.path.join(LOG_DIR, selected_file)
            df_rec = pd.read_parquet(file_path)
            st.write(f"Duration: {len(df_rec)} samples")
            rev_ceiling = st.sidebar.number_input("Review Y-Ceiling", value=5.0)
            df_rec['time_rel'] = df_rec['timestamp'] - df_rec['timestamp'].min()
            
            for i in range(6):
                st.markdown(f"**String {i+1}**")
                c = alt.Chart(df_rec).mark_line(color=STRING_COLORS[i]).encode(
                    x=alt.X('time_rel:Q', title="Time (s)"),
                    y=alt.Y(f'String {i+1}:Q', scale=alt.Scale(domain=[0, rev_ceiling]), title="RMS"),
                    tooltip=['time_rel', f'String {i+1}']
                ).properties(height=315).configure_view(fill='#181B1F').configure(background='#181B1F').interactive()
                st.altair_chart(c, use_container_width=True)
            
            if st.button("Delete Recording"):
                os.remove(file_path)
                st.rerun()4