function generateSVG(series, options) {
    // Validate inputs
    if (typeof series === 'undefined' || typeof options === 'undefined') {
        return '<svg width="800" height="450" xmlns="http://www.w3.org/2000/svg"><text x="400" y="225" text-anchor="middle" fill="#ef4444" font-size="14">Error: Invalid input</text></svg>';
    }

    var panelType = options.panelType || 'chart';

    if (panelType === 'stat') {
        return generateStatSVG(series, options);
    }

    // Responsive dimensions
    var width = options.width || 800;
    var height = options.height || 450;
    var margin = {
        top: Math.max(30, height * 0.08),
        right: Math.max(30, width * 0.05),
        bottom: Math.max(65, height * 0.18),
        left: Math.max(50, width * 0.08)
    };
    var graphWidth = width - margin.left - margin.right;
    var graphHeight = height - margin.top - margin.bottom;

    // Adaptive font sizes
    var titleSize = Math.max(12, Math.min(18, width * 0.02));
    var labelSize = Math.max(10, Math.min(14, width * 0.014));
    var tickSize = Math.max(10, Math.min(12, width * 0.012));

    // Light theme colors
    var colors = {
        background: '#ffffff',
        gridLines: '#e2e8f0',
        text: '#4a5568',
        textPrimary: '#1a202c',
        axis: '#64748b',
        series: ['#3b82f6', '#10b981', '#f59e0b', '#ef4444', '#8b5cf6', '#ec4899']
    };

    var svg = [];
    svg.push('<svg width="100%" height="100%" viewBox="0 0 ', width, ' ', height, '" preserveAspectRatio="none" xmlns="http://www.w3.org/2000/svg" style="font-family: -apple-system, BlinkMacSystemFont, \'Segoe UI\', Roboto, sans-serif;">');
    svg.push('<rect width="100%" height="100%" fill="', colors.background, '"/>');

    if (!series || !Array.isArray(series) || series.length === 0) {
        svg.push('<text x="', width / 2, '" y="', height / 2, '" text-anchor="middle" fill="#ef4444" font-size="14">Error: No data series</text></svg>');
        return svg.join('');
    }

    // Extract configuration from options (with fallbacks)
    var param1 = options.param1 || '';
    var title = options.title || 'Metric';
    var yLabel = options.yLabel || 'Value';
    var isPercentage = options.isPercentage || false;
    var transformType = options.transformType || 'none';
    var valueMultiplier = options.valueMultiplier || 1.0;
    var transformDivisor = options.transformDivisor || 1.0;
    var valueFormat = options.valueFormat || '%.2f';

    // Replace %s in title with param1
    if (title.indexOf('%s') !== -1 && param1) {
        title = title.replace('%s', param1);
    }

    // Collect all data points
    var allData = [];
    var seriesCount = series.length;
    for (var si = 0; si < seriesCount; si++) {
        var s = series[si];
        if (!s.data || !Array.isArray(s.data)) continue;
        var dataLen = s.data.length;
        for (var di = 0; di < dataLen; di++) {
            var d = s.data[di];
            if (!isNaN(d.value) && d.value >= 0) {
                allData.push({ 
                    timestamp: d.timestamp, 
                    value: d.value, 
                    seriesName: s.name 
                });
            }
        }
    }

    if (allData.length === 0) {
        svg.push('<text x="', width / 2, '" y="', height / 2, '" text-anchor="middle" fill="#ef4444" font-size="14">Error: No valid data points</text></svg>');
        return svg.join('');
    }

    // Create value formatter from format string
    var valueFormatter;
    if (valueFormat === '%d' || valueFormat === '%.0f') {
        valueFormatter = function(v) { return Math.round(v).toString(); };
    } else if (valueFormat === '%.1f') {
        valueFormatter = function(v) { return v.toFixed(1); };
    } else if (valueFormat === '%.2f') {
        valueFormatter = function(v) { return v.toFixed(2); };
    } else {
        valueFormatter = function(v) { return v.toFixed(2); };
    }

    // Apply transformations based on transformType
    var needsTransform = transformType !== 'none';
    
    if (needsTransform) {
        var allDataLen = allData.length;
        if (transformType === 'multiply') {
            for (var i = 0; i < allDataLen; i++) {
                allData[i].value *= valueMultiplier;
            }
        } else if (transformType === 'divide') {
            for (var i = 0; i < allDataLen; i++) {
                allData[i].value /= transformDivisor;
            }
        } else if (transformType === 'ps_cputime_sum') {
            // Special handling is done in C code
        }
    }

    // Calculate value range and time range
    var minVal, maxVal, minTime, maxTime;
    if (isPercentage) {
        minVal = 0;
        maxVal = 100;
    } else {
        minVal = Infinity;
        maxVal = -Infinity;
    }
    minTime = Infinity;
    maxTime = -Infinity;

    var allDataLen = allData.length;
    for (var i = 0; i < allDataLen; i++) {
        var d = allData[i];
        if (!isPercentage) {
            if (d.value < minVal) minVal = d.value;
            if (d.value > maxVal) maxVal = d.value;
        }
        if (d.timestamp < minTime) minTime = d.timestamp;
        if (d.timestamp > maxTime) maxTime = d.timestamp;
    }

    // Add padding for non-percentage
    if (!isPercentage) {
        var padding = (maxVal - minVal) * 0.1;
        if (padding === 0) padding = Math.abs(maxVal) * 0.1 || 1;
        minVal = Math.max(0, minVal - padding);
        maxVal = maxVal + padding;
    }

    var timeRange = maxTime - minTime;
    if (timeRange === 0) timeRange = 1;
    var valueRange = maxVal - minVal;

    // Main graph group
    svg.push('<g transform="translate(', margin.left, ',', margin.top, ')">');
    
    // Background panel
    svg.push('<rect width="', graphWidth, '" height="', graphHeight, '" fill="', colors.background, '" stroke="', colors.gridLines, '" stroke-width="1"/>');

    // Grid lines and labels
    var ySteps = 5;
    for (var i = 0; i <= ySteps; i++) {
        var y = (graphHeight * (1 - i / ySteps)).toFixed(2);
        var value = minVal + valueRange * (i / ySteps);
        
        svg.push('<line x1="0" y1="', y, '" x2="', graphWidth, '" y2="', y, '" stroke="', colors.gridLines, '" stroke-width="1" opacity="0.5"/>');
        svg.push('<text x="-10" y="', (parseFloat(y) + 4).toFixed(2), '" text-anchor="end" font-size="11" fill="', colors.text, '">', valueFormatter(value), '</text>');
    }

    // X-axis grid and labels
    var xSteps = Math.min(8, Math.max(4, Math.floor(graphWidth / 100)));
    
    var formatTime;
    if (timeRange > 604800) {
        formatTime = function(ts) {
            var d = new Date(ts * 1000);
            return (d.getMonth() + 1) + '/' + d.getDate();
        };
    } else if (timeRange > 86400) {
        formatTime = function(ts) {
            var d = new Date(ts * 1000);
            return (d.getMonth() + 1) + '/' + d.getDate() + ' ' + 
                   ('0' + d.getHours()).slice(-2) + ':' + ('0' + d.getMinutes()).slice(-2);
        };
    } else if (timeRange > 3600) {
        formatTime = function(ts) {
            var d = new Date(ts * 1000);
            return ('0' + d.getHours()).slice(-2) + ':' + ('0' + d.getMinutes()).slice(-2);
        };
    } else {
        formatTime = function(ts) {
            var d = new Date(ts * 1000);
            return ('0' + d.getHours()).slice(-2) + ':' + 
                   ('0' + d.getMinutes()).slice(-2) + ':' + 
                   ('0' + d.getSeconds()).slice(-2);
        };
    }

    for (var i = 0; i <= xSteps; i++) {
        var x = (graphWidth * i / xSteps).toFixed(2);
        var timestamp = minTime + timeRange * i / xSteps;
        var timeStr = formatTime(timestamp);
        
        svg.push('<line x1="', x, '" y1="0" x2="', x, '" y2="', graphHeight, '" stroke="', colors.gridLines, '" stroke-width="1" opacity="0.5"/>');
        svg.push('<text x="', x, '" y="', (graphHeight + 20), '" text-anchor="middle" font-size="11" fill="', colors.text, '">', timeStr, '</text>');
    }

    // Draw series data
    var avgStep = timeRange / (allData.length / series.length);
    var gapThreshold = avgStep * 3;

    for (var si = 0; si < seriesCount; si++) {
        var s = series[si];
        if (!s.data || s.data.length === 0) continue;
        
        var color = colors.series[si % colors.series.length];
        var validData = [];
        
        // Filter and transform valid data
        var dataLen = s.data.length;
        for (var di = 0; di < dataLen; di++) {
            var d = s.data[di];
            var val = d.value;
            
            // Apply transformations
            if (needsTransform) {
                if (transformType === 'multiply') {
                    val = val * valueMultiplier;
                } else if (transformType === 'divide') {
                    val = val / transformDivisor;
                }
            }
            
            if (!isNaN(val) && val >= 0) {
                validData.push({ timestamp: d.timestamp, value: val });
            }
        }

        if (validData.length === 0) continue;

        var escapedName = s.name.replace(/[^a-zA-Z0-9]/g, '_');
        var escapedNameJS = s.name.replace(/'/g, "\\'");

        // Group into segments
        var segments = [];
        var currentSegment = [validData[0]];
        
        for (var i = 1; i < validData.length; i++) {
            if (validData[i].timestamp - validData[i-1].timestamp > gapThreshold) {
                segments.push(currentSegment);
                currentSegment = [validData[i]];
            } else {
                currentSegment.push(validData[i]);
            }
        }
        segments.push(currentSegment);

        // Draw segments
        for (var segi = 0; segi < segments.length; segi++) {
            var segment = segments[segi];
            if (segment.length < 2) continue;
            
            var areaPath = [];
            var linePath = [];
            
            for (var i = 0; i < segment.length; i++) {
                var d = segment[i];
                var x = (graphWidth * (d.timestamp - minTime) / timeRange).toFixed(2);
                var y = (graphHeight * (1 - (Math.min(maxVal, Math.max(minVal, d.value)) - minVal) / valueRange)).toFixed(2);
                
                if (i === 0) {
                    linePath.push('M', x, ',', y);
                    areaPath.push('M', x, ',', graphHeight, ' L', x, ',', y);
                } else {
                    linePath.push(' L', x, ',', y);
                    areaPath.push(' L', x, ',', y);
                }
            }
            
            var lastPoint = segment[segment.length - 1];
            var lastX = (graphWidth * (lastPoint.timestamp - minTime) / timeRange).toFixed(2);
            areaPath.push(' L', lastX, ',', graphHeight, ' Z');
            
            var gradientId = 'g' + si + '-' + segi;
            svg.push('<defs><linearGradient id="', gradientId, '" x1="0%" y1="0%" x2="0%" y2="100%">');
            svg.push('<stop offset="0%" style="stop-color:', color, ';stop-opacity:0.3"/>');
            svg.push('<stop offset="100%" style="stop-color:', color, ';stop-opacity:0.05"/>');
            svg.push('</linearGradient></defs>');
            
            svg.push('<path d="', areaPath.join(''), '" fill="url(#', gradientId, ')"/>');
            svg.push('<path d="', linePath.join(''), '" stroke="', color, '" fill="none" stroke-width="2" stroke-linejoin="round" stroke-linecap="round"/>');
        }
        
        // Draw hover points
        for (var i = 0; i < validData.length; i++) {
            var d = validData[i];
            var x = (graphWidth * (d.timestamp - minTime) / timeRange).toFixed(2);
            var y = (graphHeight * (1 - (Math.min(maxVal, Math.max(minVal, d.value)) - minVal) / valueRange)).toFixed(2);
            var pointId = 'p-' + escapedName + '-' + i;
            
            var date = new Date(d.timestamp * 1000);
            var timeStr = ('0' + date.getHours()).slice(-2) + ':' + 
                          ('0' + date.getMinutes()).slice(-2) + ':' + 
                          ('0' + date.getSeconds()).slice(-2);
            
            svg.push('<circle id="', pointId, '" cx="', x, '" cy="', y, '" r="4" fill="', color, '" opacity="0" style="cursor: pointer;" onmouseover="showTooltip(evt, \'', escapedNameJS, '\', \'', valueFormatter(d.value), '\', \'', timeStr, '\', ', x, ', ', y, ')" onmouseout="hideTooltip()"/>');
        }
    }

    svg.push('</g>');

    // Title
    svg.push('<text x="', width / 2, '" y="25" text-anchor="middle" font-size="', titleSize, '" font-weight="600" fill="', colors.textPrimary, '">', title, '</text>');
    
    // Y-axis label
    svg.push('<text x="20" y="', height / 2, '" text-anchor="middle" transform="rotate(-90,20,', height / 2, ')" font-size="12" font-weight="500" fill="', colors.text, '">', yLabel, '</text>');
    
    // Legend
    var legendY = height - 35;
    var legendX = margin.left;
    
    svg.push('<g font-size="11">');
    for (var si = 0; si < seriesCount; si++) {
        var s = series[si];
        if (!s.data || s.data.length === 0) continue;
        
        var lastPoint = s.data[s.data.length - 1];
        if (!lastPoint || isNaN(lastPoint.value) || lastPoint.value < 0) continue;
        
        var rawValue = lastPoint.value;
        if (needsTransform) {
            if (transformType === 'multiply') {
                rawValue = rawValue * valueMultiplier;
            } else if (transformType === 'divide') {
                rawValue = rawValue / transformDivisor;
            }
        }
        
        var color = colors.series[si % colors.series.length];
        var itemWidth = 180;
        
        svg.push('<rect x="', legendX, '" y="', (legendY - 12), '" width="', itemWidth, '" height="18" fill="', colors.gridLines, '" opacity="0.3" rx="3"/>');
        svg.push('<rect x="', (legendX + 5), '" y="', (legendY - 7), '" width="10" height="10" fill="', color, '" rx="2"/>');
        svg.push('<text x="', (legendX + 20), '" y="', (legendY + 2), '" fill="', colors.textPrimary, '" font-weight="500">', s.name, ':</text>');
        svg.push('<text x="', (legendX + 90), '" y="', (legendY + 2), '" fill="', color, '" font-weight="600">', valueFormatter(rawValue), '</text>');
        
        legendX += itemWidth + 15;
        if (legendX > width - 200) {
            legendX = margin.left;
            legendY += 25;
        }
    }
    svg.push('</g>');

    // Tooltip group
    svg.push('<g id="tooltip" visibility="hidden">');
    svg.push('<rect x="0" y="0" width="160" height="50" fill="#ffffff" opacity="0.98" rx="6" stroke="', colors.gridLines, '" stroke-width="1" style="filter: drop-shadow(0px 2px 4px rgba(0,0,0,0.1))"/>');
    svg.push('<text x="8" y="18" font-size="11" fill="', colors.text, '" id="tooltip-series"></text>');
    svg.push('<text x="8" y="35" font-size="12" font-weight="600" fill="', colors.textPrimary, '" id="tooltip-value"></text>');
    svg.push('</g>');
    svg.push('</svg>');

    return svg.join('');
}

function generateStatSVG(series, options) {
    var width = options.width || 300;
    var height = options.height || 150;

    var colors = {
        background: '#ffffff',
        border: '#e2e8f0',
        text: '#4a5568',
        textPrimary: '#1a202c',
        accent: '#3b82f6',
        positive: '#10b981',
        negative: '#ef4444',
        neutral: '#6b7280',
        sparkline: '#3b82f6'
    };

    // Adaptive sizes
    var valueSize = Math.max(18, Math.min(32, height * 0.25));
    var titleSize = Math.max(10, Math.min(14, height * 0.1));
    var sparklineHeight = Math.max(12, Math.min(30, height * 0.2));
    var sparklineWidth = Math.max(40, Math.min(100, width * 0.3));

    var title = options.title || 'Metric';
    var yLabel = options.yLabel || '';
    var valueFormat = options.valueFormat || '%.2f';
    var transformType = options.transformType || 'none';
    var transformDivisor = options.transformDivisor || 1.0;

    // Collect all data points
    var allData = [];
    for (var si = 0; si < series.length; si++) {
        var s = series[si];
        if (!s.data || !Array.isArray(s.data)) continue;
        for (var di = 0; di < s.data.length; di++) {
            var d = s.data[di];
            if (!isNaN(d.value) && d.value >= 0) {
                var val = d.value;
                if (transformType === 'divide') {
                    val = val / transformDivisor;
                }
                allData.push({ timestamp: d.timestamp, value: val });
            }
        }
    }

    if (allData.length === 0) {
        return '<svg width="100%" height="100%" viewBox="0 0 ' + width + ' ' + height + '" preserveAspectRatio="none" xmlns="http://www.w3.org/2000/svg">' +
            '<rect width="100%" height="100%" fill="' + colors.background + '"/>' +
            '<text x="' + (width/2) + '" y="' + (height/2) + '" text-anchor="middle" fill="#ef4444" font-size="12">No data</text></svg>';
    }

    // Sort by timestamp
    allData.sort(function(a, b) { return a.timestamp - b.timestamp; });

    // Get current (last) and previous (first) values
    var currentValue = allData[allData.length - 1].value;
    var firstValue = allData[0].value;

    // Calculate trend
    var trend = currentValue - firstValue;
    var trendPercent = firstValue !== 0 ? (trend / firstValue * 100) : 0;

    // Format value
    var valueFormatter;
    if (valueFormat === '%d' || valueFormat === '%.0f') {
        valueFormatter = function(v) { return Math.round(v).toString(); };
    } else if (valueFormat === '%.1f') {
        valueFormatter = function(v) { return v.toFixed(1); };
    } else {
        valueFormatter = function(v) { return v.toFixed(2); };
    }

    var formattedValue = valueFormatter(currentValue);
    var formattedTrend = (trend >= 0 ? '+' : '') + valueFormatter(Math.abs(trend));
    var formattedTrendPercent = (trendPercent >= 0 ? '+' : '') + trendPercent.toFixed(1) + '%';

    // Determine trend color and arrow
    var trendColor = colors.neutral;
    var trendArrow = '→';
    if (trend > 0) {
        trendColor = colors.positive;
        trendArrow = '↑';
    } else if (trend < 0) {
        trendColor = colors.negative;
        trendArrow = '↓';
    }

    // Build sparkline path (adaptive area at bottom)
    var sparklineX = 10;
    var sparklineY = height - sparklineHeight - 10;

    var minVal = Infinity, maxVal = -Infinity;
    for (var i = 0; i < allData.length; i++) {
        if (allData[i].value < minVal) minVal = allData[i].value;
        if (allData[i].value > maxVal) maxVal = allData[i].value;
    }
    var valueRange = maxVal - minVal;
    if (valueRange === 0) valueRange = 1;

    var timeRange = allData[allData.length - 1].timestamp - allData[0].timestamp;
    if (timeRange === 0) timeRange = 1;

    var pathPoints = [];
    for (var i = 0; i < allData.length; i++) {
        var x = sparklineX + (allData[i].timestamp - allData[0].timestamp) / timeRange * sparklineWidth;
        var y = sparklineY + sparklineHeight - (allData[i].value - minVal) / valueRange * sparklineHeight;
        if (i === 0) {
            pathPoints.push('M', x.toFixed(2), ',', y.toFixed(2));
        } else {
            pathPoints.push(' L', x.toFixed(2), ',', y.toFixed(2));
        }
    }

    // Build SVG
    var svg = [];
    svg.push('<svg width="100%" height="100%" viewBox="0 0 ', width, ' ', height, '" preserveAspectRatio="none" xmlns="http://www.w3.org/2000/svg" style="font-family: -apple-system, BlinkMacSystemFont, \'Segoe UI\', Roboto, sans-serif;">');
    svg.push('<rect width="100%" height="100%" fill="', colors.background, '" stroke="', colors.border, '" stroke-width="1" rx="4"/>');

    // Title
    svg.push('<text x="12" y="22" font-size="', titleSize, '" font-weight="600" fill="', colors.text, '">', title, '</text>');

    // Main value
    svg.push('<text x="12" y="', (height * 0.45).toFixed(0), '" font-size="', valueSize, '" font-weight="700" fill="', colors.textPrimary, '">', formattedValue);
    if (yLabel) {
        svg.push('<tspan font-size="12" font-weight="400" fill="', colors.text, '"> ', yLabel, '</tspan>');
    }
    svg.push('</text>');

    // Trend indicator
    svg.push('<text x="12" y="', (height * 0.6).toFixed(0), '" font-size="12" font-weight="500">');
    svg.push('<tspan fill="', trendColor, '">', trendArrow, ' ', formattedTrend);
    if (yLabel) {
        svg.push(' (', formattedTrendPercent, ')');
    }
    svg.push('</tspan></text>');

    // Sparkline
    if (allData.length > 1) {
        svg.push('<path d="', pathPoints.join(''), '" stroke="', colors.sparkline, '" fill="none" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"/>');
    }

    svg.push('</svg>');
    return svg.join('');
}