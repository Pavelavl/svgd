package system

import "time"

// MachineProfile contains hardware and OS information
type MachineProfile struct {
	MachineID   string     `json:"machine_id"`
	Hostname    string     `json:"hostname"`
	CollectedAt time.Time  `json:"collected_at"`
	CPU         CPUInfo    `json:"cpu"`
	Memory      MemoryInfo `json:"memory"`
	OS          OSInfo     `json:"os"`
	Storage     StorageInfo `json:"storage"`
}

type CPUInfo struct {
	Model        string `json:"model"`
	Cores        int    `json:"cores"`
	Threads      int    `json:"threads"`
	FrequencyMHz int    `json:"frequency_mhz"`
	CacheL1KB    int    `json:"cache_l1_kb"`
	CacheL2KB    int    `json:"cache_l2_kb"`
	CacheL3KB    int    `json:"cache_l3_kb"`
}

type MemoryInfo struct {
	TotalMB     int `json:"total_mb"`
	AvailableMB int `json:"available_mb"`
}

type OSInfo struct {
	Name         string `json:"name"`
	Version      string `json:"version"`
	Kernel       string `json:"kernel"`
	Architecture string `json:"architecture"`
}

type StorageInfo struct {
	Type       string `json:"type"`
	MountPoint string `json:"mount_point"`
}
