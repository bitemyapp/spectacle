workspace.windowAdded.connect(function(window) {
    if (String(window.resourceClass).toLowerCase().indexOf('spectacle') === -1) return;
    print('SPECTACLE_FAST_BENCHMARK ' + JSON.stringify({time_ms: Date.now(), pid: window.pid, resource_class: String(window.resourceClass)}));
});
