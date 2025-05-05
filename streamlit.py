import streamlit as st

# HARUS DI SINI: sebelum streamlit command lainnya
st.set_page_config(page_title="Traffic Vehicle Detection Dashboard", layout="wide")

import pandas as pd
import re
from datetime import datetime
from streamlit_autorefresh import st_autorefresh
import matplotlib.pyplot as plt

# Auto-refresh setiap 10 detik
st_autorefresh(interval=10 * 1000, key="refresh")

def load_vehicle_data(log_file='traffic_debug.log'):
    pattern = r"\[DEBUG\] (\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}) - Deteksi selesai - Kendaraan: (\d+)"
    data = []

    with open(log_file, 'r') as file:
        for line in file:
            match = re.search(pattern, line)
            if match:
                waktu = datetime.strptime(match.group(1), "%Y-%m-%d %H:%M:%S")
                kendaraan = int(match.group(2))
                data.append({"Waktu": waktu, "Jumlah Kendaraan": kendaraan})

    return pd.DataFrame(data)

# Load data dari log
df = load_vehicle_data()

# START UI
st.title("🚦 Smart Traffic Monitoring Dashboard")
st.caption("Monitoring kendaraan berdasarkan hasil deteksi YOLO dari ESP32-CAM")

if df.empty:
    st.warning("⚠️ Belum ada data deteksi yang tersedia.")
    st.stop()

col1, col2 = st.columns([2, 3])

with col1:
    st.subheader("📋 Statistik Terbaru")
    last_time = df["Waktu"].iloc[-1]
    last_count = df["Jumlah Kendaraan"].iloc[-1]
    st.metric("Jumlah Kendaraan Terakhir", f"{last_count} kendaraan")
    st.metric("Waktu Deteksi", last_time.strftime("%H:%M:%S"))
    st.markdown("---")
    st.subheader("🕒 5 Deteksi Terakhir")
    st.dataframe(df.tail(5).sort_values("Waktu", ascending=False).reset_index(drop=True), use_container_width=True)

with col2:
    st.subheader("📊 Grafik Jumlah Kendaraan")
    fig, ax = plt.subplots()
    ax.plot(df["Waktu"], df["Jumlah Kendaraan"], marker='o', color='tab:blue')
    ax.set_xlabel("Waktu")
    ax.set_ylabel("Jumlah Kendaraan")
    ax.grid(True)
    st.pyplot(fig)

st.markdown("---")
st.markdown("🔄 Dashboard akan otomatis memperbarui setiap 10 detik.")
