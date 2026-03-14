package system

import (
	"crypto/rand"
	"encoding/hex"
	"os"
	"path/filepath"
)

const machineIDFile = ".svgd-machine-id"

// GetMachineID returns the machine UUID, creating it if necessary
func GetMachineID() string {
	homeDir, err := os.UserHomeDir()
	if err != nil {
		return generateFallbackID()
	}

	idPath := filepath.Join(homeDir, machineIDFile)

	// Try to read existing ID
	if data, err := os.ReadFile(idPath); err == nil {
		id := string(data)
		if len(id) == 36 { // UUID format: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
			return id
		}
	}

	// Generate new ID
	id := generateUUID()
	_ = os.WriteFile(idPath, []byte(id), 0644)
	return id
}

func generateUUID() string {
	b := make([]byte, 16)
	rand.Read(b)
	return hex.EncodeToString(b[0:4]) + "-" +
		hex.EncodeToString(b[4:6]) + "-" +
		hex.EncodeToString(b[6:8]) + "-" +
		hex.EncodeToString(b[8:10]) + "-" +
		hex.EncodeToString(b[10:16])
}

func generateFallbackID() string {
	// Fallback to hostname-based ID if home dir unavailable
	hostname, _ := os.Hostname()
	return "fallback-" + hostname
}
