package system

import (
	"encoding/csv"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"time"
)

// Reporter handles writing test results to CSV with machine context
type Reporter struct {
	testName    string
	outputDir   string
	file        *os.File
	writer      *csv.Writer
	headers     []string
	wroteHeader bool
}

// NewReporter creates a reporter for a specific test
// Results are written to tests/results/<testName>.csv
func NewReporter(testName string) (*Reporter, error) {
	repoRoot, err := findRepoRoot()
	if err != nil {
		return nil, fmt.Errorf("failed to find repo root: %w", err)
	}

	outputDir := filepath.Join(repoRoot, "tests", "results")
	if err := os.MkdirAll(outputDir, 0755); err != nil {
		return nil, fmt.Errorf("failed to create results dir: %w", err)
	}

	// Ensure machine profile is saved
	if err := SaveMachineProfile(); err != nil {
		// Non-fatal, just log
		fmt.Printf("Warning: failed to save machine profile: %v\n", err)
	}

	csvPath := filepath.Join(outputDir, testName+".csv")
	file, err := os.OpenFile(csvPath, os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0644)
	if err != nil {
		return nil, fmt.Errorf("failed to open CSV file: %w", err)
	}

	// Check if file already exists and has content
	// If so, read the existing header to avoid duplicating it
	info, err := os.Stat(csvPath)
	existingHeaders := []string{}
	if err == nil && info.Size() > 0 {
		// Read existing file to get headers
		file.Close()
		file, err = os.Open(csvPath)
		if err != nil {
			return nil, fmt.Errorf("failed to open CSV file for reading: %w", err)
		}
		reader := csv.NewReader(file)
		headers, err := reader.Read()
		file.Close()
		if err == nil && len(headers) > 0 {
			existingHeaders = headers
		}
		// Re-open in append mode
		file, err = os.OpenFile(csvPath, os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0644)
		if err != nil {
			return nil, fmt.Errorf("failed to reopen CSV file: %w", err)
		}
	}

	return &Reporter{
		testName:  testName,
		outputDir: outputDir,
		file:      file,
		writer:    csv.NewWriter(file),
		headers:   existingHeaders,
		wroteHeader: len(existingHeaders) > 0,
	}, nil
}

// Record writes a result row to the CSV
// Automatically adds machine_id, timestamp, and test_name
func (r *Reporter) Record(row map[string]any) error {
	if !r.wroteHeader {
		// Build headers from first row keys
		keys := make([]string, 0, len(row))
		for k := range row {
			keys = append(keys, k)
		}
		sort.Strings(keys)

		// Merge with existing base headers
		baseHeaders := []string{"machine_id", "timestamp", "test_name"}
		newHeaders := append(baseHeaders, keys...)

		// If we already have headers from file, verify they match
		if len(r.headers) > 0 {
			// Check if row keys match existing headers (excluding base ones)
			for _, h := range r.headers {
				// Skip base headers, check data headers
				if h == "machine_id" || h == "timestamp" || h == "test_name" {
					continue
				}
				// If existing header not in row keys, it's a new run with different columns
				// We can safely write if we have all required columns
			}
			// Use existing headers from file
		} else {
			r.headers = newHeaders
		}

		// Write header
		if err := r.writer.Write(r.headers); err != nil {
			return err
		}
		r.wroteHeader = true
	}

	// Build record
	record := make([]string, len(r.headers))
	for i, h := range r.headers {
		switch h {
		case "machine_id":
			record[i] = GetMachineID()
		case "timestamp":
			record[i] = time.Now().UTC().Format(time.RFC3339)
		case "test_name":
			record[i] = r.testName
		default:
			if v, ok := row[h]; ok {
				record[i] = fmt.Sprintf("%v", v)
			}
		}
	}

	return r.writer.Write(record)
}

// Close flushes and closes the CSV file
func (r *Reporter) Close() error {
 r.writer.Flush()
 if err := r.writer.Error(); err != nil {
  r.file.Close()
  return err
 }
 return r.file.Close()
}

// SaveMachineProfile saves the current machine profile to results/machines/
func SaveMachineProfile() error {
 profile := GetProfile()

 repoRoot, err := findRepoRoot()
 if err != nil {
  return err
 }

 machinesDir := filepath.Join(repoRoot, "tests", "results", "machines")
 if err := os.MkdirAll(machinesDir, 0755); err != nil {
  return err
 }

 profilePath := filepath.Join(machinesDir, profile.MachineID+".json")
 data, err := json.MarshalIndent(profile, "", "  ")
 if err != nil {
  return err
 }

 return os.WriteFile(profilePath, data, 0644)
}

func findRepoRoot() (string, error) {
 dir, err := os.Getwd()
 if err != nil {
  return "", err
 }

 for {
  // Check for project root indicators
  // Look for config.json + makefile (project root, not submodules)
  makefilePath := filepath.Join(dir, "makefile")
  configJson := filepath.Join(dir, "config.json")
  if _, err := os.Stat(makefilePath); err == nil {
   if _, err := os.Stat(configJson); err == nil {
    return dir, nil
   }
  }
  // Also check Makefile (capital M)
  MakefilePath := filepath.Join(dir, "Makefile")
  if _, err := os.Stat(MakefilePath); err == nil {
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
