package main

import (
	"encoding/csv"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"time"
)

type MachineProfile struct {
	MachineID   string `json:"machine_id"`
	Hostname    string `json:"hostname"`
	CollectedAt string `json:"collected_at"`
	CPU         struct {
		Model string `json:"model"`
		Cores int    `json:"cores"`
	} `json:"cpu"`
	Memory struct {
		TotalMB int `json:"total_mb"`
	} `json:"memory"`
	OS struct {
		Name string `json:"name"`
	} `json:"os"`
}

func main() {
	repoRoot, err := findRepoRoot()
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error: %v\n", err)
		os.Exit(1)
	}

	resultsDir := filepath.Join(repoRoot, "tests", "results")
	machinesDir := filepath.Join(resultsDir, "machines")
	chartsDir := filepath.Join(resultsDir, "charts", "output")
	reportPath := filepath.Join(resultsDir, "report.md")

	// Load machine profiles
	profiles := loadMachineProfiles(machinesDir)

	// Load all results
	results := loadAllResults(resultsDir)

	// Generate report
	report := generateReport(profiles, results, chartsDir)

	// Write report
	if err := os.WriteFile(reportPath, []byte(report), 0644); err != nil {
		fmt.Fprintf(os.Stderr, "Error writing report: %v\n", err)
		os.Exit(1)
	}

	fmt.Printf("Report generated: %s\n", reportPath)
}

func findRepoRoot() (string, error) {
	dir, err := os.Getwd()
	if err != nil {
		return "", err
	}

	for {
		// Check for project root indicators
		makefile := filepath.Join(dir, "makefile")
		configJson := filepath.Join(dir, "config.json")
		if _, err := os.Stat(makefile); err == nil {
			if _, err := os.Stat(configJson); err == nil {
				return dir, nil
			}
		}
		// Also check Makefile (capital M)
		Makefile := filepath.Join(dir, "Makefile")
		if _, err := os.Stat(Makefile); err == nil {
			if _, err := os.Stat(configJson); err == nil {
				return dir, nil
			}
		}

		parent := filepath.Dir(dir)
		if parent == dir {
			return "", fmt.Errorf("could not find repo root")
		}
		dir = parent
	}
}

func loadMachineProfiles(dir string) map[string]MachineProfile {
	profiles := make(map[string]MachineProfile)

	files, err := filepath.Glob(filepath.Join(dir, "*.json"))
	if err != nil {
		return profiles
	}

	for _, f := range files {
		data, err := os.ReadFile(f)
		if err != nil {
			continue
		}

		var p MachineProfile
		if err := json.Unmarshal(data, &p); err != nil {
			continue
		}

		profiles[p.MachineID] = p
	}

	return profiles
}

func loadAllResults(dir string) map[string][][]string {
	results := make(map[string][][]string)

	files, err := filepath.Glob(filepath.Join(dir, "*.csv"))
	if err != nil {
		return results
	}

	for _, f := range files {
		file, err := os.Open(f)
		if err != nil {
			continue
		}

		reader := csv.NewReader(file)
		records, err := reader.ReadAll()
		file.Close()

		if err != nil || len(records) == 0 {
			continue
		}

		name := strings.TrimSuffix(filepath.Base(f), ".csv")
		results[name] = records
	}

	return results
}

func generateReport(profiles map[string]MachineProfile, results map[string][][]string, chartsDir string) string {
	var sb strings.Builder

	sb.WriteString("# Benchmark Report\n\n")
	sb.WriteString(fmt.Sprintf("**Generated:** %s\n\n", time.Now().Format("2006-01-02 15:04:05")))

	// Count total runs
	totalRuns := 0
	for _, records := range results {
		totalRuns += len(records) - 1 // Exclude header
	}
	sb.WriteString(fmt.Sprintf("**Total runs:** %d\n\n", totalRuns))

	// Machines section
	sb.WriteString("## Machines\n\n")
	if len(profiles) > 0 {
		sb.WriteString("| ID | Hostname | CPU | Cores | RAM | OS |\n")
		sb.WriteString("|----|----------|-----|-------|-----|----|\n")

		// Sort machines by hostname for consistent output
		var machineIDs []string
		for id := range profiles {
			machineIDs = append(machineIDs, id)
		}
		sort.Strings(machineIDs)

		for _, id := range machineIDs {
			p := profiles[id]
			shortID := id
			if len(id) > 8 {
				shortID = id[:8]
			}
			ramGB := p.Memory.TotalMB / 1024
			sb.WriteString(fmt.Sprintf("| %s | %s | %s | %d | %dGB | %s |\n",
				shortID, p.Hostname, p.CPU.Model, p.CPU.Cores, ramGB, p.OS.Name))
		}
	} else {
		sb.WriteString("*No machine profiles found*\n")
	}
	sb.WriteString("\n---\n\n")

	// Charts section (if charts exist)
	charts := []struct {
		file string
		name string
	}{
		{"throughput_comparison.png", "Throughput Comparison"},
		{"latency_heatmap.png", "Latency Heatmap"},
		{"efficiency.png", "Efficiency (RPS vs Memory)"},
		{"memory_usage.png", "Memory Usage"},
		{"load_test_summary.png", "Load Test Summary"},
	}

	hasCharts := false
	for _, c := range charts {
		chartPath := filepath.Join(chartsDir, c.file)
		if _, err := os.Stat(chartPath); err == nil {
			if !hasCharts {
				sb.WriteString("## Charts\n\n")
				hasCharts = true
			}
			sb.WriteString(fmt.Sprintf("### %s\n\n", c.name))
			sb.WriteString(fmt.Sprintf("![%s](charts/output/%s)\n\n", c.name, c.file))
		}
	}

	if hasCharts {
		sb.WriteString("---\n\n")
	}

	// Results sections for each test type
	testOrder := []string{"comparison", "load", "e2e"}
	for _, testName := range testOrder {
		records, ok := results[testName]
		if !ok || len(records) < 2 {
			continue
		}

		sb.WriteString(fmt.Sprintf("## %s Tests\n\n", strings.Title(testName)))

		header := records[0]
		data := records[1:]

		// Find machine_id column index
		machineIDIdx := -1
		for i, h := range header {
			if h == "machine_id" {
				machineIDIdx = i
				break
			}
		}

		// Limit columns to avoid too wide tables
		maxCols := len(header)
		if maxCols > 10 {
			maxCols = 10
		}

		// Build table header
		sb.WriteString("|")
		for i := 0; i < maxCols; i++ {
			h := header[i]
			if h == "machine_id" {
				sb.WriteString(" Machine |")
			} else {
				sb.WriteString(fmt.Sprintf(" %s |", h))
			}
		}
		sb.WriteString("\n|")
		for i := 0; i < maxCols; i++ {
			sb.WriteString("------|")
		}
		sb.WriteString("\n")

		// Build table rows
		for _, row := range data {
			sb.WriteString("|")
			for i := 0; i < maxCols && i < len(row); i++ {
				val := row[i]
				if i == machineIDIdx {
					if p, ok := profiles[val]; ok {
						sb.WriteString(fmt.Sprintf(" %s |", p.Hostname))
					} else {
						shortID := val
						if len(val) > 8 {
							shortID = val[:8]
						}
						sb.WriteString(fmt.Sprintf(" %s |", shortID))
					}
				} else {
					// Truncate long values
					if len(val) > 20 {
						val = val[:17] + "..."
					}
					sb.WriteString(fmt.Sprintf(" %s |", val))
				}
			}
			sb.WriteString("\n")
		}
		sb.WriteString("\n")
	}

	// Add any other result files not in testOrder
	for testName, records := range results {
		found := false
		for _, t := range testOrder {
			if t == testName {
				found = true
				break
			}
		}
		if found || len(records) < 2 {
			continue
		}

		sb.WriteString(fmt.Sprintf("## %s Results\n\n", strings.Title(testName)))

		header := records[0]
		data := records[1:]

		// Build simple table
		sb.WriteString("|")
		for _, h := range header {
			sb.WriteString(fmt.Sprintf(" %s |", h))
		}
		sb.WriteString("\n|")
		for range header {
			sb.WriteString("------|")
		}
		sb.WriteString("\n")

		for _, row := range data {
			sb.WriteString("|")
			for _, val := range row {
				if len(val) > 20 {
					val = val[:17] + "..."
				}
				sb.WriteString(fmt.Sprintf(" %s |", val))
			}
			sb.WriteString("\n")
		}
		sb.WriteString("\n")
	}

	return sb.String()
}
