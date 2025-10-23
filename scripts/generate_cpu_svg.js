function generateSVG(series, options) {
    // Validate inputs
    if (typeof series === 'undefined' || typeof options === 'undefined') {
        return '<svg width="600" height="400" xmlns="http://www.w3.org/2000/svg"><text x="300" y="200" text-anchor="middle" fill="red">Error: Invalid input</text></svg>';
    }

    var width = 600;
    var height = 400;
    var margin = { top: 30, right: 30, bottom: 70, left: 60 };
    var graphWidth = width - margin.left - margin.right;
    var graphHeight = height - margin.top - margin.bottom;

    var svg = '<svg width="' + width + '" height="' + height + '" xmlns="http://www.w3.org/2000/svg" style="font-family: Arial, sans-serif;">';
    svg += '<rect width="' + width + '" height="' + height + '" fill="#ffffff"/>';

    if (!series || !Array.isArray(series) || series.length === 0) {
        svg += '<text x="' + (width / 2) + '" y="' + (height / 2) + '" text-anchor="middle" fill="red">Error: No data series</text>';
        svg += '</svg>';
        return svg;
    }

    var metricType = options.metricType || 'unknown';
    var param1 = options.param1 || '';
    var allData = [];
    series.forEach(function(s) {
        if (!s.data || !Array.isArray(s.data)) return;
        s.data.forEach(function(d) {
            if (!isNaN(d.value) && d.value >= 0) {
                allData.push({ timestamp: d.timestamp, value: d.value, seriesName: s.name });
            }
        });
    });

    if (allData.length === 0) {
        svg += '<text x="' + (width / 2) + '" y="' + (height / 2) + '" text-anchor="middle" fill="red">Error: No valid data points</text>';
        svg += '</svg>';
        return svg;
    }

    var title = '';
    var yLabel = '';
    var isPercentage = false;
    var valueFormatter = function(value) { return value.toFixed(1); };
    var minVal, maxVal;

    switch (metricType) {
        case 'cpu_total':
            title = 'CPU Utilization';
            yLabel = 'Usage (%)';
            isPercentage = true;
            break;
        case 'cpu_process':
            title = 'CPU Utilization for ' + param1;
            yLabel = 'CPU Time (s)';
            break;
        case 'ram_total':
            title = 'RAM Utilization';
            yLabel = 'Usage (%)';
            isPercentage = true;
            break;
        case 'ram_process':
            title = 'Memory Usage for ' + param1;
            yLabel = 'Memory (MB)';
            allData = allData.map(function(d) { return { timestamp: d.timestamp, value: d.value / 1024 / 1024, seriesName: d.seriesName }; });
            valueFormatter = function(value) { return value.toFixed(1) + ' MB'; };
            break;
        case 'network':
            title = 'Network Traffic for ' + param1;
            yLabel = 'Traffic (bytes/s)';
            break;
        case 'disk':
            title = 'Disk Operations for ' + param1;
            yLabel = 'Operations/s';
            break;
        case 'postgresql_connections':
            title = 'PostgreSQL Connections';
            yLabel = 'Connections';
            break;
        default:
            title = 'Metric';
            yLabel = 'Value';
    }

    if (isPercentage) {
        minVal = 0;
        maxVal = 100;
    } else {
        var values = allData.map(function(d) { return d.value; });
        minVal = Math.min.apply(Math, values);
        maxVal = Math.max.apply(Math, values);
        if (maxVal === minVal) {
            maxVal = minVal + 1;
        } else if (maxVal - minVal < 0.1) {
            maxVal += (maxVal - minVal) * 0.1;
        }
    }

    svg += '<g transform="translate(' + margin.left + ',' + margin.top + ')">';
    svg += '<rect width="' + graphWidth + '" height="' + graphHeight + '" fill="#f8f9fc" stroke="#ddd"/>';

    var ySteps = 5;
    svg += '<g stroke-width="1">';
    for (var i = 0; i <= ySteps; i++) {
        var y = graphHeight * (1 - i / ySteps);
        var value = minVal + (maxVal - minVal) * (i / ySteps);
        svg += '<line x1="0" y1="' + y + '" x2="' + graphWidth + '" y2="' + y + '" stroke="#eee" />';
        svg += '<text x="-5" y="' + (y + 4) + '" text-anchor="end" font-size="10" fill="#666">' + valueFormatter(value) + '</text>';
    }
    svg += '</g>';

    var xSteps = 5;
    var timestamps = allData.map(function(d) { return d.timestamp; });
    var minTime = Math.min.apply(Math, timestamps);
    var maxTime = Math.max.apply(Math, timestamps);

    if (maxTime === minTime) {
        maxTime = minTime + 1;
    }
    svg += '<g stroke-width="1">';
    for (var i = 0; i <= xSteps; i++) {
        var x = graphWidth * i / xSteps;
        var timestamp = minTime + (maxTime - minTime) * i / xSteps;
        var date = new Date(timestamp * 1000);
        
        // Адаптивное форматирование в зависимости от диапазона
        var timeRange = maxTime - minTime;
        var timeStr;
        if (timeRange > 86400 * 7) {
            // Больше недели - показываем дату
            timeStr = (date.getMonth() + 1) + '/' + date.getDate();
        } else if (timeRange > 86400) {
            // Больше дня - показываем день и время
            timeStr = (date.getMonth() + 1) + '/' + date.getDate() + ' ' + 
                      date.getHours() + ':' + ('0' + date.getMinutes()).slice(-2);
        } else {
            // Меньше дня - только время
            timeStr = date.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' });
        }
        
        svg += '<line x1="' + x + '" y1="0" x2="' + x + '" y2="' + graphHeight + '" stroke="#eee"/>';
        svg += '<text x="' + x + '" y="' + (graphHeight + 20) + '" text-anchor="middle" font-size="10" fill="#666">' + timeStr + '</text>';
    }
    svg += '</g>';

    var colors = ['#4e73df', '#1cc88a', '#e74a3b', '#36b9cc'];
    var tooltipId = 'tooltip';
    series.forEach(function(s, index) {
        var validData = s.data.filter(function(d) { return !isNaN(d.value) && d.value >= 0; });
        if (validData.length === 0) return;
    
        // Группировка данных в непрерывные сегменты
        var segments = [];
        var currentSegment = [];
        for (var i = 0; i < validData.length; i++) {
            currentSegment.push(validData[i]);
            // Проверяем, является ли следующая точка не непрерывной (большой разрыв во времени)
            if (i < validData.length - 1) {
                var currentTime = validData[i].timestamp;
                var nextTime = validData[i + 1].timestamp;
                var timeDiff = nextTime - currentTime;
                // Предполагаем, что разрыв больше 2 шагов времени (можно настроить)
                var maxTimeGap = 2 * (maxTime - minTime) / validData.length;
                if (timeDiff > maxTimeGap) {
                    segments.push(currentSegment);
                    currentSegment = [];
                }
            }
        }
        if (currentSegment.length > 0) {
            segments.push(currentSegment);
        }
    
        // Отрисовка каждого сегмента как отдельного пути
        segments.forEach(function(segment) {
            if (segment.length < 2) return; // Для линии нужно минимум 2 точки
            var path = '';
            segment.forEach(function(d, i) {
                var x = graphWidth * (d.timestamp - minTime) / (maxTime - minTime);
                var value = (metricType === 'ram_process') ? d.value / 1024 / 1024 : d.value;
                var y = graphHeight * (1 - (Math.min(maxVal, Math.max(minVal, value)) - minVal) / (maxVal - minVal));
                path += (i === 0 ? 'M' : ' L') + x.toFixed(2) + ',' + y.toFixed(2);
            });
            svg += '<path d="' + path + '" stroke="' + colors[index % colors.length] + '" fill="none" stroke-width="2" stroke-linejoin="round"/>';
        });
    
        // Отрисовка точек для hover-эффекта
        validData.forEach(function(d, i) {
            var x = graphWidth * (d.timestamp - minTime) / (maxTime - minTime);
            var value = (metricType === 'ram_process') ? d.value / 1024 / 1024 : d.value;
            var y = graphHeight * (1 - (Math.min(maxVal, Math.max(minVal, value)) - minVal) / (maxVal - minVal));
            var pointId = 'point-' + s.name + '-' + i;
            var date = new Date(d.timestamp * 1000);
            var timeStr = date.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' });
            svg += '<circle id="' + pointId + '" cx="' + x.toFixed(2) + '" cy="' + y.toFixed(2) + '" r="5" fill="' + colors[index % colors.length] + '" opacity="0" onmouseover="showTooltip(evt, \'' + s.name + '\', \'' + valueFormatter(value) + '\', \'' + timeStr + '\', ' + x.toFixed(2) + ', ' + y.toFixed(2) + ')" onmouseout="hideTooltip()"/>';
        });
    });

    svg += '</g>';

    // Add tooltip group
    svg += '<g id="' + tooltipId + '" visibility="hidden">';
    svg += '<rect x="0" y="0" width="150" height="40" fill="#333" opacity="0.8" rx="5"/>';
    svg += '<text x="5" y="15" font-size="10" fill="#fff" id="tooltip-series"></text>';
    svg += '<text x="5" y="30" font-size="10" fill="#fff" id="tooltip-value"></text>';
    svg += '</g>';

    // Add JavaScript for hover effects
    svg += '<script><![CDATA[';
    svg += 'function showTooltip(evt, series, value, time, x, y) {';
    svg += '  var tooltip = document.getElementById("' + tooltipId + '");';
    svg += '  var seriesText = document.getElementById("tooltip-series");';
    svg += '  var valueText = document.getElementById("tooltip-value");';
    svg += '  seriesText.textContent = "Series: " + series;';
    svg += '  valueText.textContent = value + " at " + time;';
    svg += '  tooltip.setAttribute("transform", "translate(" + (x + 10) + "," + (y - 50) + ")");';
    svg += '  tooltip.setAttribute("visibility", "visible");';
    svg += '  evt.target.setAttribute("r", "7");';
    svg += '  evt.target.setAttribute("opacity", "1");';
    svg += '}';
    svg += 'function hideTooltip() {';
    svg += '  var tooltip = document.getElementById("' + tooltipId + '");';
    svg += '  tooltip.setAttribute("visibility", "hidden");';
    svg += '  var circles = document.getElementsByTagName("circle");';
    svg += '  for (var i = 0; i < circles.length; i++) {';
    svg += '    circles[i].setAttribute("r", "5");';
    svg += '    circles[i].setAttribute("opacity", "0");';
    svg += '  }';
    svg += '}';
    svg += ']]></script>';

    svg += '<text x="' + (width / 2) + '" y="20" text-anchor="middle" font-size="16" fill="#333">' + title + '</text>';
    svg += '<text x="10" y="' + (height / 2) + '" text-anchor="middle" transform="rotate(-90,10,' + (height / 2) + ')" font-size="12" fill="#666">' + yLabel + '</text>';

    var legendY = height - 20;
    svg += '<g font-size="12">';
    var currentX = 20;
    series.forEach(function(s, index) {
        var lastPoint = s.data[s.data.length - 1];
        if (lastPoint && !isNaN(lastPoint.value) && lastPoint.value >= 0) {
            var value = (metricType === 'ram_process') ? lastPoint.value / 1024 / 1024 : lastPoint.value;
            var date = new Date(lastPoint.timestamp * 1000);
            var timeStr = date.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' });
            svg += '<circle cx="' + currentX + '" cy="' + (legendY - 5) + '" r="5" fill="' + colors[index % colors.length] + '"/>';
            svg += '<text x="' + (currentX + 10) + '" y="' + legendY + '" fill="#333">' + s.name + ': ' + valueFormatter(value) + ' at ' + timeStr + '</text>';
            currentX += 220;
        }
    });
    svg += '</g>';

    svg += '</svg>';
    return svg;
}