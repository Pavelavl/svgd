// ===== Configuration =====
const config = {
    apiBaseUrl: 'http://localhost:8080',
    refreshInterval: 10000,
    panels: [],
    customTimeRange: null,
    currentPeriod: 3600,
    dashboardName: 'System Monitoring Dashboard'
};

const defaultPanels = [
    { id: 'cpu', endpoint: 'cpu', title: 'CPU Total Usage' },
    { id: 'ram', endpoint: 'ram', title: 'RAM Total Usage' },
    { id: 'network-eth0', endpoint: 'network/eth0', title: 'Network I/O (eth0)' },
    { id: 'postgresql', endpoint: 'postgresql/connections', title: 'PostgreSQL Connections' }
];

// Theme configuration for SVG
const svgThemes = {
    dark: {
        background: '#1f2023',
        gridLines: '#2d2e32',
        text: '#9fa6b2',
        textPrimary: '#ffffff'
    },
    light: {
        background: '#ffffff',
        gridLines: '#e2e8f0',
        text: '#4a5568',
        textPrimary: '#1a202c'
    }
};

let refreshTimer = null;
let refreshCountdown = 10;
let searchTerm = '';

// ===== Toast Notifications =====
function showToast(message, type = 'info', duration = 3000) {
    const container = document.getElementById('toastContainer');
    const toast = document.createElement('div');
    toast.className = `toast ${type}`;
    
    const icon = type === 'success' ? '✓' : type === 'error' ? '✕' : 'ℹ';
    toast.innerHTML = `
        <div style="font-size: 1.25rem;">${icon}</div>
        <div style="flex-grow: 1;">${message}</div>
        <button onclick="this.parentElement.remove()" style="cursor: pointer; opacity: 0.7;">✕</button>
    `;
    
    container.appendChild(toast);
    
    setTimeout(() => {
        toast.style.opacity = '0';
        setTimeout(() => toast.remove(), 300);
    }, duration);
}

// ===== Theme Management =====
function applySVGTheme(svgElement, theme) {
    const colors = svgThemes[theme];
    const darkColors = svgThemes.dark;
    const lightColors = svgThemes.light;
    
    const backgrounds = svgElement.querySelectorAll(`rect[fill="${darkColors.background}"], rect[fill="${lightColors.background}"]`);
    backgrounds.forEach(rect => {
        if (!rect.getAttribute('opacity') && !rect.closest('#tooltip')) {
            rect.setAttribute('fill', colors.background);
        }
    });
    
    const gridLines = svgElement.querySelectorAll(`line[stroke="${darkColors.gridLines}"], line[stroke="${lightColors.gridLines}"]`);
    gridLines.forEach(line => line.setAttribute('stroke', colors.gridLines));
    
    const textElements = svgElement.querySelectorAll(`text[fill="${darkColors.text}"], text[fill="${lightColors.text}"]`);
    textElements.forEach(text => text.setAttribute('fill', colors.text));
    
    const primaryTextElements = svgElement.querySelectorAll(`text[fill="${darkColors.textPrimary}"], text[fill="${lightColors.textPrimary}"]`);
    primaryTextElements.forEach(text => text.setAttribute('fill', colors.textPrimary));
    
    const legendBgs = svgElement.querySelectorAll('rect[opacity="0.3"]');
    legendBgs.forEach(rect => {
        const currentFill = rect.getAttribute('fill');
        if (currentFill === darkColors.gridLines || currentFill === lightColors.gridLines) {
            rect.setAttribute('fill', colors.gridLines);
        }
    });
    
    const tooltip = svgElement.querySelector('#tooltip');
    if (tooltip) {
        const tooltipBg = tooltip.querySelector('rect');
        if (tooltipBg) {
            if (theme === 'dark') {
                tooltipBg.setAttribute('fill', '#000000');
                tooltipBg.setAttribute('opacity', '0.9');
                tooltipBg.setAttribute('stroke', colors.gridLines);
            } else {
                tooltipBg.setAttribute('fill', '#ffffff');
                tooltipBg.setAttribute('opacity', '0.98');
                tooltipBg.setAttribute('stroke', colors.gridLines);
            }
        }
        const tooltipTexts = tooltip.querySelectorAll('text');
        tooltipTexts.forEach((text, index) => {
            if (index === 0) {
                text.setAttribute('fill', colors.text);
            } else {
                text.setAttribute('fill', colors.textPrimary);
            }
        });
    }
}

function applyThemeToAllSVGs() {
    const theme = document.body.classList.contains('light-theme') ? 'light' : 'dark';
    document.querySelectorAll('.graph-container svg').forEach(svg => {
        applySVGTheme(svg, theme);
    });
}

// ===== Tooltip Functions =====
window.showTooltip = function(evt, series, value, time, dataX, dataY) {
    const svgElement = evt.target.closest('svg');
    if (!svgElement) return;
    
    const tooltip = svgElement.querySelector('#tooltip');
    const seriesText = svgElement.querySelector('#tooltip-series');
    const valueText = svgElement.querySelector('#tooltip-value');
    
    if (!tooltip || !seriesText || !valueText) return;
    
    const viewBox = svgElement.viewBox.baseVal;
    const svgWidth = viewBox.width || 800;
    const margin = { top: 45, right: 40, bottom: 80, left: 75 };
    
    let tooltipX = dataX + margin.left + 10;
    let tooltipY = dataY + margin.top - 60;
    
    if (tooltipX > svgWidth - 170) {
        tooltipX = dataX + margin.left - 170;
    }
    
    if (tooltipY < 0) {
        tooltipY = dataY + margin.top + 15;
    }
    
    seriesText.textContent = series;
    valueText.textContent = value + ' at ' + time;
    
    tooltip.setAttribute('transform', 'translate(' + tooltipX + ',' + tooltipY + ')');
    tooltip.setAttribute('visibility', 'visible');
    
    evt.target.setAttribute('r', '6');
    evt.target.setAttribute('opacity', '1');
};

window.hideTooltip = function() {
    document.querySelectorAll('#tooltip').forEach(tooltip => {
        tooltip.setAttribute('visibility', 'hidden');
    });
    
    document.querySelectorAll('circle[id^="p-"]').forEach(circle => {
        circle.setAttribute('r', '4');
        circle.setAttribute('opacity', '0');
    });
};

// ===== API Functions =====
async function fetchSVG(endpoint, period) {
    try {
        const url = `${config.apiBaseUrl}/${endpoint}?period=${period}`;
        const response = await fetch(url);

        if (!response.ok) {
            throw new Error(`HTTP ${response.status}`);
        }

        const contentType = response.headers.get('content-type');
        if (contentType && contentType.includes('image/svg+xml')) {
            return { success: true, svg: await response.text() };
        } else {
            const data = await response.json();
            return { success: false, error: data.error || 'Unknown error' };
        }
    } catch (error) {
        return { success: false, error: error.message };
    }
}

// ===== Panel Management =====
function createPanel(panelConfig) {
    const panel = document.createElement('div');
    panel.className = 'panel';
    panel.id = `panel-${panelConfig.id}`;

    panel.innerHTML = `
        <div class="panel-header">
            <div class="panel-title">
                <svg class="icon" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                    <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9 19v-6a2 2 0 00-2-2H5a2 2 0 00-2 2v6a2 2 0 002 2h2a2 2 0 002-2zm0 0V9a2 2 0 012-2h2a2 2 0 012 2v10m-6 0a2 2 0 002 2h2a2 2 0 002-2m0 0V5a2 2 0 012-2h2a2 2 0 012 2v14a2 2 0 01-2 2h-2a2 2 0 01-2-2z"/>
                </svg>
                <span class="panel-title-text">${panelConfig.title}</span>
            </div>
            <div style="display: flex; gap: 0.5rem; align-items: center;">
                <span class="badge badge-success" id="status-${panelConfig.id}" style="display: none;">●</span>
                <button class="btn btn-sm" onclick="exportPanel('${panelConfig.id}', 'svg')" title="Export SVG">
                    <svg class="icon" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                        <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M4 16v1a3 3 0 003 3h10a3 3 0 003-3v-1m-4-4l-4 4m0 0l-4-4m4 4V4"/>
                    </svg>
                </button>
                <button class="btn btn-sm" onclick="toggleFullscreen('${panelConfig.id}')" title="Fullscreen">
                    <svg class="icon" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                        <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M4 8V4m0 0h4M4 4l5 5m11-1V4m0 0h-4m4 0l-5 5M4 16v4m0 0h4m-4 0l5-5m11 5l-5-5m5 5v-4m0 4h-4"/>
                    </svg>
                </button>
                <button class="btn btn-sm" onclick="refreshPanel('${panelConfig.id}')" title="Refresh">
                    <svg class="icon" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                        <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M4 4v5h.582m15.356 2A8.001 8.001 0 004.582 9m0 0H9m11 11v-5h-.581m0 0a8.003 8.003 0 01-15.357-2m15.357 2H15"/>
                    </svg>
                </button>
                <button class="btn btn-sm" onclick="removePanel('${panelConfig.id}')" title="Remove">
                    <svg class="icon" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                        <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M6 18L18 6M6 6l12 12"/>
                    </svg>
                </button>
            </div>
        </div>
        <div class="graph-container" id="graph-${panelConfig.id}">
            <div class="loading"></div>
        </div>
    `;

    return panel;
}

async function updatePanel(panelConfig) {
    const graphContainer = document.getElementById(`graph-${panelConfig.id}`);
    const statusBadge = document.getElementById(`status-${panelConfig.id}`);

    if (!graphContainer) return;

    graphContainer.innerHTML = '<div class="loading"></div>';

    const result = await fetchSVG(panelConfig.endpoint, config.currentPeriod);

    if (result.success) {
        graphContainer.innerHTML = result.svg;
        
        const theme = document.body.classList.contains('light-theme') ? 'light' : 'dark';
        const svgElement = graphContainer.querySelector('svg');
        if (svgElement) {
            applySVGTheme(svgElement, theme);
        }
        
        if (statusBadge) {
            statusBadge.className = 'badge badge-success';
            statusBadge.style.display = 'inline-block';
        }
        updateStatusIndicator(true);
    } else {
        graphContainer.innerHTML = `<div class="error-message">Error: ${result.error}<br><small>Check if svgd-gateway is running on ${config.apiBaseUrl}</small></div>`;
        if (statusBadge) {
            statusBadge.className = 'badge badge-error';
            statusBadge.style.display = 'inline-block';
        }
        updateStatusIndicator(false);
    }
}

async function refreshPanel(panelId) {
    const panel = config.panels.find(p => p.id === panelId);
    if (panel) {
        await updatePanel(panel);
        showToast('Panel refreshed', 'success', 2000);
    }
}

function removePanel(panelId) {
    const panelElement = document.getElementById(`panel-${panelId}`);
    if (panelElement) {
        panelElement.style.opacity = '0';
        setTimeout(() => {
            panelElement.remove();
            config.panels = config.panels.filter(p => p.id !== panelId);
            savePanelsToStorage();
            updatePanelCount();
            showToast('Panel removed', 'info', 2000);
        }, 300);
    }
}

async function updateAllPanels() {
    const visiblePanels = config.panels.filter(p => {
        if (!searchTerm) return true;
        return p.title.toLowerCase().includes(searchTerm.toLowerCase());
    });
    
    for (const panel of visiblePanels) {
        await updatePanel(panel);
    }
    document.getElementById('lastUpdate').textContent = new Date().toLocaleTimeString();
}

function addPanel(panelConfig) {
    if (config.panels.find(p => p.id === panelConfig.id)) {
        showToast('Panel already exists!', 'error');
        return;
    }

    config.panels.push(panelConfig);
    const panelsContainer = document.getElementById('panelsContainer');
    panelsContainer.appendChild(createPanel(panelConfig));

    updatePanel(panelConfig);
    savePanelsToStorage();
    updatePanelCount();
    showToast('Panel added successfully', 'success');
}

function toggleFullscreen(panelId) {
    const panel = document.getElementById(`panel-${panelId}`);
    if (panel) {
        panel.classList.toggle('fullscreen');
    }
}

function exportPanel(panelId, format) {
    const graphContainer = document.getElementById(`graph-${panelId}`);
    const svg = graphContainer.querySelector('svg');
    
    if (!svg) {
        showToast('No graph to export', 'error');
        return;
    }
    
    const svgData = new XMLSerializer().serializeToString(svg);
    const blob = new Blob([svgData], { type: 'image/svg+xml' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `panel-${panelId}-${Date.now()}.svg`;
    a.click();
    URL.revokeObjectURL(url);
    
    showToast('Panel exported', 'success');
}

// ===== Dashboard Management =====
function initializeDashboard() {
    const savedPanels = localStorage.getItem('svgd-panels');
    const savedConfig = localStorage.getItem('svgd-config');
    
    if (savedConfig) {
        try {
            const parsed = JSON.parse(savedConfig);
            config.apiBaseUrl = parsed.apiBaseUrl || config.apiBaseUrl;
            config.refreshInterval = parsed.refreshInterval || config.refreshInterval;
            config.dashboardName = parsed.dashboardName || config.dashboardName;
            
            document.getElementById('apiBaseUrl').value = config.apiBaseUrl;
            document.getElementById('refreshInterval').value = config.refreshInterval;
            document.getElementById('dashboardName').value = config.dashboardName;
            document.getElementById('dashboardTitleEdit').value = config.dashboardName;
        } catch (e) {
            console.error('Failed to load config:', e);
        }
    }
    
    if (savedPanels) {
        try {
            config.panels = JSON.parse(savedPanels);
        } catch (e) {
            config.panels = [...defaultPanels];
        }
    } else {
        config.panels = [...defaultPanels];
    }

    const panelsContainer = document.getElementById('panelsContainer');
    panelsContainer.innerHTML = '';

    config.panels.forEach(panelConfig => {
        panelsContainer.appendChild(createPanel(panelConfig));
    });

    updateAllPanels();
    updatePanelCount();
    startRefreshTimer();
}

function savePanelsToStorage() {
    localStorage.setItem('svgd-panels', JSON.stringify(config.panels));
}

function saveConfigToStorage() {
    const configToSave = {
        apiBaseUrl: config.apiBaseUrl,
        refreshInterval: config.refreshInterval,
        dashboardName: config.dashboardName
    };
    localStorage.setItem('svgd-config', JSON.stringify(configToSave));
    showToast('Configuration saved', 'success', 2000);
}

function exportDashboard(format) {
    const exportData = {
        version: '1.0',
        timestamp: new Date().toISOString(),
        config: {
            apiBaseUrl: config.apiBaseUrl,
            refreshInterval: config.refreshInterval,
            dashboardName: config.dashboardName
        },
        panels: config.panels
    };
    
    const json = JSON.stringify(exportData, null, 2);
    const blob = new Blob([json], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `dashboard-${Date.now()}.json`;
    a.click();
    URL.revokeObjectURL(url);
    
    showToast('Dashboard configuration exported', 'success');
    closeDropdown('exportDropdown');
}

function importDashboard() {
    const configText = document.getElementById('importConfig').value;
    
    try {
        const imported = JSON.parse(configText);
        
        if (imported.panels) {
            config.panels = imported.panels;
            savePanelsToStorage();
        }
        
        if (imported.config) {
            config.apiBaseUrl = imported.config.apiBaseUrl || config.apiBaseUrl;
            config.refreshInterval = imported.config.refreshInterval || config.refreshInterval;
            config.dashboardName = imported.config.dashboardName || config.dashboardName;
            saveConfigToStorage();
        }
        
        closeModal('importModal');
        location.reload();
    } catch (e) {
        showToast('Invalid configuration file', 'error');
    }
}

function takeSnapshot() {
    showToast('Creating snapshot...', 'info', 1500);
    
    setTimeout(() => {
        try {
            // Get current timestamp
            const timestamp = new Date();
            const timestampStr = timestamp.toISOString().replace(/[:.]/g, '-').slice(0, -5);
            
            // Collect all SVGs
            const panels = [];
            config.panels.forEach(panel => {
                const graphContainer = document.getElementById(`graph-${panel.id}`);
                const svg = graphContainer ? graphContainer.querySelector('svg') : null;
                
                if (svg) {
                    panels.push({
                        id: panel.id,
                        title: panel.title,
                        svg: new XMLSerializer().serializeToString(svg)
                    });
                }
            });
            
            if (panels.length === 0) {
                showToast('No panels to snapshot', 'error');
                closeDropdown('exportDropdown');
                return;
            }
            
            // Create HTML snapshot
            const theme = document.body.classList.contains('light-theme') ? 'light' : 'dark';
            const snapshotHTML = `<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Dashboard Snapshot - ${timestamp.toLocaleString()}</title>
    <style>
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }
        
        body {
            font-family: 'Inter', -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
            background: ${theme === 'dark' ? '#0b0c0e' : '#f7f8fa'};
            color: ${theme === 'dark' ? '#ffffff' : '#1a202c'};
            padding: 2rem;
        }
        
        .header {
            max-width: 1800px;
            margin: 0 auto 2rem;
            padding: 1.5rem;
            background: ${theme === 'dark' ? '#17181a' : '#ffffff'};
            border: 1px solid ${theme === 'dark' ? '#2d2e32' : '#e2e8f0'};
            border-radius: 8px;
        }
        
        .header h1 {
            font-size: 1.5rem;
            margin-bottom: 0.5rem;
        }
        
        .header .meta {
            font-size: 0.875rem;
            color: ${theme === 'dark' ? '#9fa6b2' : '#4a5568'};
        }
        
        .info-box {
            background: ${theme === 'dark' ? 'rgba(59, 130, 246, 0.1)' : 'rgba(59, 130, 246, 0.05)'};
            border: 1px solid ${theme === 'dark' ? 'rgba(59, 130, 246, 0.2)' : 'rgba(59, 130, 246, 0.15)'};
            border-radius: 6px;
            padding: 1rem;
            margin-top: 1rem;
            font-size: 0.875rem;
        }
        
        .panels {
            max-width: 1800px;
            margin: 0 auto;
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(500px, 1fr));
            gap: 1.5rem;
        }
        
        .panel {
            background: ${theme === 'dark' ? '#1f2023' : '#ffffff'};
            border: 1px solid ${theme === 'dark' ? '#2d2e32' : '#e2e8f0'};
            border-radius: 8px;
            padding: 1.5rem;
            box-shadow: 0 2px 8px rgba(0, 0, 0, ${theme === 'dark' ? '0.3' : '0.1'});
        }
        
        .panel-title {
            font-size: 1rem;
            font-weight: 600;
            margin-bottom: 1rem;
            padding-bottom: 0.75rem;
            border-bottom: 1px solid ${theme === 'dark' ? '#2d2e32' : '#e2e8f0'};
        }
        
        .panel svg {
            width: 100%;
            height: auto;
        }
        
        .footer {
            max-width: 1800px;
            margin: 2rem auto 0;
            text-align: center;
            color: ${theme === 'dark' ? '#9fa6b2' : '#4a5568'};
            font-size: 0.75rem;
            padding-top: 1rem;
            border-top: 1px solid ${theme === 'dark' ? '#2d2e32' : '#e2e8f0'};
        }
        
        @media print {
            body {
                background: white;
            }
            .panel {
                page-break-inside: avoid;
            }
        }
    </style>
</head>
<body>
    <div class="header">
        <h1>📊 ${config.dashboardName}</h1>
        <div class="meta">
            <div><strong>Snapshot taken:</strong> ${timestamp.toLocaleString()}</div>
            <div><strong>Time range:</strong> ${document.getElementById('currentTimeRange').textContent}</div>
            <div><strong>Number of panels:</strong> ${panels.length}</div>
        </div>
        <div class="info-box">
            ℹ️ This is a static snapshot of the dashboard. Data is frozen at the time of capture.
            To view live data, use the main dashboard application.
        </div>
    </div>
    
    <div class="panels">
        ${panels.map(p => `
            <div class="panel">
                <div class="panel-title">${p.title}</div>
                ${p.svg}
            </div>
        `).join('')}
    </div>
    
    <div class="footer">
        <p>Generated by SVGD Metrics Dashboard</p>
        <p>API Server: ${config.apiBaseUrl}</p>
    </div>
</body>
</html>`;
            
            // Download the snapshot
            const blob = new Blob([snapshotHTML], { type: 'text/html' });
            const url = URL.createObjectURL(blob);
            const a = document.createElement('a');
            a.href = url;
            a.download = `dashboard-snapshot-${timestampStr}.html`;
            a.click();
            URL.revokeObjectURL(url);
            
            showToast(`Snapshot saved: ${panels.length} panels captured`, 'success');
        } catch (error) {
            console.error('Snapshot error:', error);
            showToast('Failed to create snapshot', 'error');
        }
    }, 100);
    
    closeDropdown('exportDropdown');
}

function resetDashboard() {
    if (confirm('Are you sure you want to reset the dashboard to defaults? This will remove all panels and settings.')) {
        localStorage.clear();
        location.reload();
    }
}

function updatePanelCount() {
    document.getElementById('panelCount').textContent = `${config.panels.length} panel${config.panels.length !== 1 ? 's' : ''}`;
}

// ===== UI Functions =====
function openModal(modalId) {
    document.getElementById(modalId)?.classList.add('show');
}

function closeModal(modalId) {
    document.getElementById(modalId)?.classList.remove('show');
}

function toggleDropdown(dropdownId) {
    const dropdown = document.getElementById(dropdownId);
    const allDropdowns = document.querySelectorAll('.dropdown');
    
    allDropdowns.forEach(d => {
        if (d.id !== dropdownId) {
            d.classList.remove('show');
        }
    });
    
    dropdown?.classList.toggle('show');
}

function closeDropdown(dropdownId) {
    document.getElementById(dropdownId)?.classList.remove('show');
}

function updateStatusIndicator(success) {
    const statusDot = document.getElementById('statusDot');
    const statusText = document.getElementById('statusText');

    if (success) {
        statusDot.className = 'status-dot active';
        statusText.textContent = 'Live';
    } else {
        statusDot.className = 'status-dot error';
        statusText.textContent = 'Error';
    }
}

function applyCustomTimeRange() {
    const from = document.getElementById('customTimeFrom').value;
    const to = document.getElementById('customTimeTo').value;
    
    if (from && to) {
        showToast('Custom time range applied', 'success');
        closeModal('customTimeModal');
        // Implement custom time range logic here
    } else {
        showToast('Please select both start and end times', 'error');
    }
}

// ===== Refresh Timer =====
function startRefreshTimer() {
    stopRefreshTimer();
    
    const interval = parseInt(document.getElementById('refreshInterval').value);
    if (interval === 0) return;
    
    refreshCountdown = interval / 1000;
    
    refreshTimer = setInterval(() => {
        refreshCountdown--;
        document.getElementById('refreshCountdown').textContent = `${refreshCountdown}s`;
        
        if (refreshCountdown <= 0) {
            updateAllPanels();
            refreshCountdown = interval / 1000;
        }
    }, 1000);
}

function stopRefreshTimer() {
    if (refreshTimer) {
        clearInterval(refreshTimer);
        refreshTimer = null;
    }
}

// ===== Event Listeners =====
document.addEventListener('DOMContentLoaded', () => {
    // Theme toggle
    document.getElementById('themeToggle').addEventListener('click', function () {
        document.body.classList.toggle('light-theme');
        const theme = document.body.classList.contains('light-theme') ? 'light' : 'dark';
        localStorage.setItem('svgd-theme', theme);
        applyThemeToAllSVGs();
    });

    // Refresh button
    document.getElementById('refreshBtn').addEventListener('click', function () {
        updateAllPanels();
        const svg = this.querySelector('svg');
        if (svg) {
            svg.style.transform = 'rotate(360deg)';
            svg.style.transition = 'transform 0.5s';
            setTimeout(() => {
                svg.style.transform = '';
            }, 500);
        }
    });

    // Add panel button
    document.getElementById('addPanelBtn').addEventListener('click', () => openModal('addPanelModal'));

    // Sidebar toggle
    document.getElementById('sidebarToggle').addEventListener('click', () => {
        document.getElementById('sidebar').classList.toggle('show');
    });

    // Time range dropdown
    document.querySelector('#timeRangeDropdown .btn').addEventListener('click', () => {
        toggleDropdown('timeRangeDropdown');
    });

    document.querySelectorAll('#timeRangeDropdown .dropdown-item[data-period]').forEach(item => {
        item.addEventListener('click', function() {
            const period = this.getAttribute('data-period');
            config.currentPeriod = parseInt(period);
            document.getElementById('currentTimeRange').textContent = this.textContent;
            closeDropdown('timeRangeDropdown');
            updateAllPanels();
        });
    });

    // Export dropdown
    document.querySelector('#exportDropdown .btn').addEventListener('click', () => {
        toggleDropdown('exportDropdown');
    });

    // Search panels
    document.getElementById('searchPanels').addEventListener('input', function(e) {
        searchTerm = e.target.value;
        const panels = document.querySelectorAll('.panel');
        
        panels.forEach(panel => {
            const title = panel.querySelector('.panel-title-text').textContent.toLowerCase();
            if (title.includes(searchTerm.toLowerCase())) {
                panel.style.display = 'block';
            } else {
                panel.style.display = 'none';
            }
        });
    });

    // Add panel form
    document.getElementById('newPanelType').addEventListener('change', function () {
        const parameterGroup = document.getElementById('parameterGroup');
        const parameterLabel = document.getElementById('parameterLabel');
        const value = this.value;

        if (value === 'cpu/process' || value === 'ram/process') {
            parameterGroup.style.display = 'block';
            parameterLabel.textContent = 'Process Name';
        } else if (value === 'network') {
            parameterGroup.style.display = 'block';
            parameterLabel.textContent = 'Interface Name (e.g., eth0)';
        } else if (value === 'disk') {
            parameterGroup.style.display = 'block';
            parameterLabel.textContent = 'Disk Name (e.g., sda)';
        } else {
            parameterGroup.style.display = 'none';
        }
    });

    document.getElementById('addPanelForm').addEventListener('submit', function (e) {
        e.preventDefault();

        const type = document.getElementById('newPanelType').value;
        const parameter = document.getElementById('newPanelParameter').value.trim();
        const customTitle = document.getElementById('newPanelTitle').value.trim();

        let endpoint = type;
        let title = customTitle || type;
        let id = type.replace(/\//g, '-');

        if (parameter && (type.includes('process') || type === 'network' || type === 'disk')) {
            endpoint = `${type}/${parameter}`;
            id = `${id}-${parameter}`;
            if (!customTitle) {
                title = `${type} (${parameter})`;
            }
        }

        addPanel({ id, endpoint, title });
        closeModal('addPanelModal');

        document.getElementById('addPanelForm').reset();
        document.getElementById('parameterGroup').style.display = 'none';
    });

    // Settings listeners
    document.getElementById('apiBaseUrl').addEventListener('change', function() {
        config.apiBaseUrl = this.value;
        saveConfigToStorage();
    });

    document.getElementById('refreshInterval').addEventListener('change', function() {
        config.refreshInterval = parseInt(this.value);
        saveConfigToStorage();
        startRefreshTimer();
    });

    document.getElementById('dashboardName').addEventListener('change', function() {
        config.dashboardName = this.value;
        document.getElementById('dashboardTitleEdit').value = this.value;
        saveConfigToStorage();
    });

    document.getElementById('dashboardTitleEdit').addEventListener('change', function() {
        config.dashboardName = this.value;
        document.getElementById('dashboardName').value = this.value;
        saveConfigToStorage();
    });

    // Keyboard shortcuts - only Escape for closing modals
    document.addEventListener('keydown', function (e) {
        if (e.key === 'Escape') {
            document.querySelectorAll('.modal.show').forEach(modal => {
                modal.classList.remove('show');
            });
            document.querySelectorAll('.dropdown.show').forEach(dropdown => {
                dropdown.classList.remove('show');
            });
        }
    });

    // Close dropdowns when clicking outside
    document.addEventListener('click', function(e) {
        if (!e.target.closest('.dropdown')) {
            document.querySelectorAll('.dropdown').forEach(dropdown => {
                dropdown.classList.remove('show');
            });
        }
    });

    // Load saved theme
    const savedTheme = localStorage.getItem('svgd-theme');
    if (savedTheme === 'dark') {
        document.body.classList.remove('light-theme');
    } else {
        document.body.classList.add('light-theme');
    }

    // Initialize dashboard
    initializeDashboard();
});