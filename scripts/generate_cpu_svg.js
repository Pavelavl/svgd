function generateSVG(series, options) {
    // Validate inputs
    if (typeof series === 'undefined' || typeof options === 'undefined') {
        return '<svg width="800" height="450" xmlns="http://www.w3.org/2000/svg"><text x="400" y="225" text-anchor="middle" fill="#ef4444" font-size="14">Error: Invalid input</text></svg>';
    }

    var width = 800;
    var height = 450;
    var margin = { top: 45, right: 40, bottom: 80, left: 75 };
    var graphWidth = width - margin.left - margin.right;
    var graphHeight = height - margin.top - margin.bottom;

    // Dark theme colors (Grafana-style)
    var colors = {
        background: '#1f2023',
        gridLines: '#2d2e32',
        text: '#9fa6b2',
        textPrimary: '#ffffff',
        axis: '#4a5568',
        series: ['#3b82f6', '#10b981', '#f59e0b', '#ef4444', '#8b5cf6', '#ec4899']
    };

    var svg = '<svg viewBox="0 0 ' + width + ' ' + height + '" xmlns="http://www.w3.org/2000/svg" style="font-family: -apple-system, BlinkMacSystemFont, \'Segoe UI\', Roboto, sans-serif; width: 100%; height: 100%;" preserveAspectRatio="xMidYMid meet">';
    
    // Background
    svg += '<rect width="100%" height="100%" fill="' + colors.background + '"/>';

    if (!series || !Array.isArray(series) || series.length === 0) {
        svg += '<text x="' + (width / 2) + '" y="' + (height / 2) + '" text-anchor="middle" fill="#ef4444" font-size="14">Error: No data series</text></svg>';
        return svg;
    }

    var metricType = options.metricType || 'unknown';
    var param1 = options.param1 || '';
    
    // Collect all data points
    var allData = [];
    series.forEach(function(s) {
        if (!s.data || !Array.isArray(s.data)) return;
        s.data.forEach(function(d) {
            if (!isNaN(d.value) && d.value >= 0) {
                allData.push({ 
                    timestamp: d.timestamp, 
                    value: d.value, 
                    seriesName: s.name 
                });
            }
        });
    });

    if (allData.length === 0) {
        svg += '<text x="' + (width / 2) + '" y="' + (height / 2) + '" text-anchor="middle" fill="#ef4444" font-size="14">Error: No valid data points</text></svg>';
        return svg;
    }

    var title = '';
    var yLabel = '';
    var isPercentage = false;
    var valueFormatter = function(value) { return value.toFixed(2); };
    var minVal, maxVal;

    switch (metricType) {
        case 'cpu_total':
            title = 'CPU Utilization';
            yLabel = 'Usage (%)';
            isPercentage = true;
            allData = allData.map(function(d) { 
                return { timestamp: d.timestamp, value: d.value * 100, seriesName: d.seriesName }; 
            });
            valueFormatter = function(value) { return value.toFixed(1); };
            break;
        case 'cpu_process':
            title = 'CPU Time for ' + param1;
            yLabel = 'CPU Time (seconds)';
            valueFormatter = function(value) { return value.toFixed(2); };
            break;
        case 'ram_total':
            title = 'RAM Utilization';
            yLabel = 'Usage (%)';
            isPercentage = true;
            valueFormatter = function(value) { return value.toFixed(1); };
            break;
        case 'ram_process':
            title = 'Memory Usage for ' + param1;
            yLabel = 'Memory (MB)';
            allData = allData.map(function(d) { 
                return { timestamp: d.timestamp, value: d.value / 1024 / 1024, seriesName: d.seriesName }; 
            });
            valueFormatter = function(value) { return value.toFixed(1); };
            break;
        case 'network':
            title = 'Network Traffic for ' + param1;
            if (value >= 1024*1024*1024) {
                yLabel = 'Traffic (GB/s)';
            } else if (value >= 1024*1024) {
                yLabel = 'Traffic (MB/s)';
            } else if (value >= 1024) {
                yLabel = 'Traffic (KB/s)';
            } else {
                yLabel = 'Traffic (B/s)';
            }
            valueFormatter = function (value) {
                if (value >= 1024*1024*1024) return (value / 1024*1024*1024).toFixed(2);
                if (value >= 1024*1024) return (value / 1024*1024).toFixed(2);
                if (value >= 1024) return (value / 1024).toFixed(2);
                return value.toFixed(2);
            };
            break;
        case 'disk':
            title = 'Disk Operations for ' + param1;
            yLabel = 'Operations/s';
            valueFormatter = function(value) { return value.toFixed(1); };
            break;
        case 'postgresql_connections':
            title = 'PostgreSQL Connections';
            yLabel = 'Connections';
            valueFormatter = function(value) { return Math.round(value).toString(); };
            break;
        default:
            title = 'Metric';
            yLabel = 'Value';
    }

    // Calculate value range
    if (isPercentage) {
        minVal = 0;
        maxVal = 100;
    } else {
        var values = allData.map(function(d) { return d.value; });
        minVal = Math.min.apply(Math, values);
        maxVal = Math.max.apply(Math, values);
        
        // Add padding
        var padding = (maxVal - minVal) * 0.1;
        if (padding === 0) padding = Math.abs(maxVal) * 0.1 || 1;
        minVal = Math.max(0, minVal - padding);
        maxVal = maxVal + padding;
    }

    // Main graph group
    svg += '<g transform="translate(' + margin.left + ',' + margin.top + ')">';
    
    // Background panel
    svg += '<rect width="' + graphWidth + '" height="' + graphHeight + '" fill="' + colors.background + '" stroke="' + colors.gridLines + '" stroke-width="1"/>';

    // Grid lines
    var ySteps = 5;
    for (var i = 0; i <= ySteps; i++) {
        var y = graphHeight * (1 - i / ySteps);
        var value = minVal + (maxVal - minVal) * (i / ySteps);
        
        // Horizontal grid line
        svg += '<line x1="0" y1="' + y.toFixed(2) + '" x2="' + graphWidth + '" y2="' + y.toFixed(2) + '" stroke="' + colors.gridLines + '" stroke-width="1" opacity="0.5"/>';
        
        // Y-axis label
        svg += '<text x="-10" y="' + (y + 4).toFixed(2) + '" text-anchor="end" font-size="11" fill="' + colors.text + '">' + valueFormatter(value) + '</text>';
    }

    // X-axis grid and labels
    var timestamps = allData.map(function(d) { return d.timestamp; });
    var minTime = Math.min.apply(Math, timestamps);
    var maxTime = Math.max.apply(Math, timestamps);
    if (maxTime === minTime) maxTime = minTime + 1;

    var timeRange = maxTime - minTime;
    var xSteps = Math.min(8, Math.max(4, Math.floor(graphWidth / 100)));
    
    for (var i = 0; i <= xSteps; i++) {
        var x = graphWidth * i / xSteps;
        var timestamp = minTime + timeRange * i / xSteps;
        var date = new Date(timestamp * 1000);
        
        // Adaptive time formatting
        var timeStr;
        if (timeRange > 86400 * 7) {
            timeStr = (date.getMonth() + 1) + '/' + date.getDate();
        } else if (timeRange > 86400) {
            timeStr = (date.getMonth() + 1) + '/' + date.getDate() + ' ' + 
                      ('0' + date.getHours()).slice(-2) + ':' + ('0' + date.getMinutes()).slice(-2);
        } else if (timeRange > 3600) {
            timeStr = ('0' + date.getHours()).slice(-2) + ':' + ('0' + date.getMinutes()).slice(-2);
        } else {
            timeStr = ('0' + date.getHours()).slice(-2) + ':' + 
                      ('0' + date.getMinutes()).slice(-2) + ':' + 
                      ('0' + date.getSeconds()).slice(-2);
        }
        
        // Vertical grid line
        svg += '<line x1="' + x.toFixed(2) + '" y1="0" x2="' + x.toFixed(2) + '" y2="' + graphHeight + '" stroke="' + colors.gridLines + '" stroke-width="1" opacity="0.5"/>';
        
        // X-axis label
        svg += '<text x="' + x.toFixed(2) + '" y="' + (graphHeight + 20) + '" text-anchor="middle" font-size="11" fill="' + colors.text + '">' + timeStr + '</text>';
    }

    // Draw series data with area fill
    series.forEach(function(s, index) {
        var validData = s.data.filter(function(d) { return !isNaN(d.value) && d.value >= 0; });
        if (validData.length === 0) return;
        
        var color = colors.series[index % colors.series.length];
        
        // Group data into continuous segments
        var segments = [];
        var currentSegment = [];
        
        for (var i = 0; i < validData.length; i++) {
            currentSegment.push(validData[i]);
            
            if (i < validData.length - 1) {
                var timeDiff = validData[i + 1].timestamp - validData[i].timestamp;
                var avgStep = timeRange / validData.length;
                if (timeDiff > avgStep * 3) {
                    segments.push(currentSegment);
                    currentSegment = [];
                }
            }
        }
        if (currentSegment.length > 0) segments.push(currentSegment);
        
        // Draw each segment
        segments.forEach(function(segment) {
            if (segment.length < 2) return;
            
            // Create area path
            var areaPath = '';
            var linePath = '';
            
            segment.forEach(function(d, i) {
                var x = graphWidth * (d.timestamp - minTime) / timeRange;
                var rawValue = (metricType === 'ram_process') ? d.value / 1024 / 1024 : d.value;
                var y = graphHeight * (1 - (Math.min(maxVal, Math.max(minVal, rawValue)) - minVal) / (maxVal - minVal));
                
                if (i === 0) {
                    linePath = 'M' + x.toFixed(2) + ',' + y.toFixed(2);
                    areaPath = 'M' + x.toFixed(2) + ',' + graphHeight + ' L' + x.toFixed(2) + ',' + y.toFixed(2);
                } else {
                    linePath += ' L' + x.toFixed(2) + ',' + y.toFixed(2);
                    areaPath += ' L' + x.toFixed(2) + ',' + y.toFixed(2);
                }
            });
            
            var lastPoint = segment[segment.length - 1];
            var lastX = graphWidth * (lastPoint.timestamp - minTime) / timeRange;
            areaPath += ' L' + lastX.toFixed(2) + ',' + graphHeight + ' Z';
            
            // Draw area with gradient
            var gradientId = 'gradient-' + index + '-' + segments.indexOf(segment);
            svg += '<defs><linearGradient id="' + gradientId + '" x1="0%" y1="0%" x2="0%" y2="100%">';
            svg += '<stop offset="0%" style="stop-color:' + color + ';stop-opacity:0.3"/>';
            svg += '<stop offset="100%" style="stop-color:' + color + ';stop-opacity:0.05"/>';
            svg += '</linearGradient></defs>';
            
            svg += '<path d="' + areaPath + '" fill="url(#' + gradientId + ')"/>';
            svg += '<path d="' + linePath + '" stroke="' + color + '" fill="none" stroke-width="2" stroke-linejoin="round" stroke-linecap="round"/>';
        });
        
        // Draw hover points
        validData.forEach(function(d, i) {
            var x = graphWidth * (d.timestamp - minTime) / timeRange;
            var rawValue = (metricType === 'ram_process') ? d.value / 1024 / 1024 : d.value;
            var y = graphHeight * (1 - (Math.min(maxVal, Math.max(minVal, rawValue)) - minVal) / (maxVal - minVal));
            var pointId = 'point-' + s.name.replace(/[^a-zA-Z0-9]/g, '_') + '-' + i;
            var date = new Date(d.timestamp * 1000);
            var timeStr = ('0' + date.getHours()).slice(-2) + ':' + 
                          ('0' + date.getMinutes()).slice(-2) + ':' + 
                          ('0' + date.getSeconds()).slice(-2);
            
            svg += '<circle id="' + pointId + '" cx="' + x.toFixed(2) + '" cy="' + y.toFixed(2) + '" r="4" fill="' + color + '" opacity="0" style="cursor: pointer;" onmouseover="showTooltip(evt, \'' + s.name + '\', \'' + valueFormatter(rawValue) + '\', \'' + timeStr + '\', ' + x.toFixed(2) + ', ' + y.toFixed(2) + ')" onmouseout="hideTooltip()"/>';
        });
    });

    svg += '</g>';

    // Title
    svg += '<text x="' + (width / 2) + '" y="25" text-anchor="middle" font-size="16" font-weight="600" fill="' + colors.textPrimary + '">' + title + '</text>';
    
    // Y-axis label
    svg += '<text x="20" y="' + (height / 2) + '" text-anchor="middle" transform="rotate(-90,20,' + (height / 2) + ')" font-size="12" font-weight="500" fill="' + colors.text + '">' + yLabel + '</text>';
    
    // Legend
    var legendY = height - 35;
    var legendX = margin.left;
    
    svg += '<g font-size="11">';
    series.forEach(function(s, index) {
        if (!s.data || s.data.length === 0) return;
        
        var lastPoint = s.data[s.data.length - 1];
        if (!lastPoint || isNaN(lastPoint.value) || lastPoint.value < 0) return;
        
        var rawValue = (metricType === 'ram_process') ? lastPoint.value / 1024 / 1024 : lastPoint.value;
        var color = colors.series[index % colors.series.length];
        
        // Legend item background
        var itemWidth = 180;
        svg += '<rect x="' + legendX + '" y="' + (legendY - 12) + '" width="' + itemWidth + '" height="18" fill="' + colors.gridLines + '" opacity="0.3" rx="3"/>';
        
        // Color indicator
        svg += '<rect x="' + (legendX + 5) + '" y="' + (legendY - 7) + '" width="10" height="10" fill="' + color + '" rx="2"/>';
        
        // Series name and value
        svg += '<text x="' + (legendX + 20) + '" y="' + (legendY + 2) + '" fill="' + colors.textPrimary + '" font-weight="500">' + s.name + ':</text>';
        svg += '<text x="' + (legendX + 90) + '" y="' + (legendY + 2) + '" fill="' + color + '" font-weight="600">' + valueFormatter(rawValue) + '</text>';
        
        legendX += itemWidth + 15;
        if (legendX > width - 200) {
            legendX = margin.left;
            legendY += 25;
        }
    });
    svg += '</g>';

    // Tooltip group
    svg += '<g id="tooltip" visibility="hidden">';
    svg += '<rect x="0" y="0" width="160" height="50" fill="#000000" opacity="0.9" rx="6"/>';
    svg += '<text x="8" y="18" font-size="11" fill="' + colors.text + '" id="tooltip-series"></text>';
    svg += '<text x="8" y="35" font-size="12" font-weight="600" fill="' + colors.textPrimary + '" id="tooltip-value"></text>';
    svg += '</g>';
    svg += '</svg>';

    return svg;
}