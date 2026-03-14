package system

import (
	"bufio"
	"os"
	"os/exec"
	"runtime"
	"strconv"
	"strings"
	"time"
)

// GetProfile collects machine hardware and OS information
func GetProfile() *MachineProfile {
	return &MachineProfile{
		MachineID:   GetMachineID(),
		Hostname:    getHostname(),
		CollectedAt: time.Now().UTC(),
		CPU:         getCPUInfo(),
		Memory:      getMemoryInfo(),
		OS:          getOSInfo(),
		Storage:     getStorageInfo(),
	}
}

func getHostname() string {
	h, _ := os.Hostname()
	return h
}

func getCPUInfo() CPUInfo {
	info := CPUInfo{
		Cores:   runtime.NumCPU(),
		Threads: runtime.NumCPU(),
	}

	// Read /proc/cpuinfo on Linux
	if f, err := os.Open("/proc/cpuinfo"); err == nil {
		defer f.Close()
		scanner := bufio.NewScanner(f)
		for scanner.Scan() {
			line := scanner.Text()
			if strings.HasPrefix(line, "model name") {
				parts := strings.SplitN(line, ":", 2)
				if len(parts) == 2 {
					info.Model = strings.TrimSpace(parts[1])
				}
			}
			if strings.HasPrefix(line, "cpu MHz") {
				parts := strings.SplitN(line, ":", 2)
				if len(parts) == 2 {
					if mhz, err := strconv.Atoi(strings.TrimSpace(strings.Split(parts[1], ".")[0])); err == nil {
						info.FrequencyMHz = mhz
					}
				}
			}
			if strings.HasPrefix(line, "cache size") {
				parts := strings.SplitN(line, ":", 2)
				if len(parts) == 2 {
					// Parse "16384 KB" format
					kbStr := strings.TrimSpace(strings.ReplaceAll(parts[1], " KB", ""))
					if kb, err := strconv.Atoi(kbStr); err == nil {
						info.CacheL3KB = kb
					}
				}
			}
		}
	}
	return info
}

func getMemoryInfo() MemoryInfo {
	info := MemoryInfo{}

	// Read /proc/meminfo
	if f, err := os.Open("/proc/meminfo"); err == nil {
		defer f.Close()
		scanner := bufio.NewScanner(f)
		for scanner.Scan() {
			line := scanner.Text()
			if strings.HasPrefix(line, "MemTotal:") {
				parts := strings.Fields(line)
				if len(parts) >= 2 {
					if kb, err := strconv.Atoi(parts[1]); err == nil {
						info.TotalMB = kb / 1024
					}
				}
			}
			if strings.HasPrefix(line, "MemAvailable:") {
				parts := strings.Fields(line)
				if len(parts) >= 2 {
					if kb, err := strconv.Atoi(parts[1]); err == nil {
						info.AvailableMB = kb / 1024
					}
				}
			}
		}
	}
	return info
}

func getOSInfo() OSInfo {
	info := OSInfo{
		Architecture: runtime.GOARCH,
	}

	// Read /etc/os-release
	if f, err := os.Open("/etc/os-release"); err == nil {
		defer f.Close()
		scanner := bufio.NewScanner(f)
		for scanner.Scan() {
			line := scanner.Text()
			if strings.HasPrefix(line, "NAME=") {
				info.Name = strings.Trim(strings.TrimPrefix(line, "NAME="), "\"")
			}
			if strings.HasPrefix(line, "VERSION=") {
				info.Version = strings.Trim(strings.TrimPrefix(line, "VERSION="), "\"")
			}
		}
	}

	// Get kernel version
	if out, err := exec.Command("uname", "-r").Output(); err == nil {
		info.Kernel = strings.TrimSpace(string(out))
	}

	return info
}

func getStorageInfo() StorageInfo {
	info := StorageInfo{
		MountPoint: "/",
		Type:       "unknown",
	}

	// Check if root is SSD using lsblk
	if out, err := exec.Command("lsblk", "-d", "-o", "ROTA", "-n").Output(); err == nil {
		lines := strings.Split(strings.TrimSpace(string(out)), "\n")
		if len(lines) > 0 {
			if strings.TrimSpace(lines[0]) == "0" {
				info.Type = "ssd"
			} else {
				info.Type = "hdd"
			}
		}
	}

	return info
}
