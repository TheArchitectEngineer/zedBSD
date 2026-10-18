(function () {
  var passed = 0, total = 0;
  function check(name, actual, expected) {
    total++;
    var ok = actual === expected;
    if (ok) passed++;
    console.log('css-numbers ' + name + ': ' + (ok ? 'PASS' : 'FAIL actual=' + actual + ' expected=' + expected));
  }
  var cases = [['1','1'],['23.4e5','2.34e+06'],['+.678E9','6.78e+08'],['.0','0'],['-0','0'],['+1','1'],['0.123456789','0.123457'],['1.234565','1.23456'],['1e-40','1e-40'],['1e-100','1e-100'],['1e-8','1e-08'],['1e1000','3.40282e+38'],['1e-1000','0'],['1/**/','1']];
  var props = ['flex-grow','flex-shrink'];
  for (var p = 0; p < props.length; p++) {
    var name = props[p], member = p ? 'flexShrink' : 'flexGrow';
    for (var i = 0; i < cases.length; i++) {
      var e = document.createElement('div');
      e.style.setProperty(name, cases[i][0]);
      check(name + '-' + i + '-getter', e.style.getPropertyValue(name), cases[i][1]);
      check(name + '-' + i + '-member', e.style[member], cases[i][1]);
      check(name + '-' + i + '-text', e.style.cssText, name + ': ' + cases[i][1] + ';');
      check(name + '-' + i + '-attribute', e.getAttribute('style'), name + ': ' + cases[i][1] + ';');
      check(name + '-' + i + '-remove', e.style.removeProperty(name), cases[i][1]);
    }
    var d = document.createElement('div');
    d.style.cssText = name + ': +.678E9 !important;';
    check(name + '-cssText', d.style[member], '6.78e+08');
    check(name + '-priority', d.style.getPropertyPriority(name), 'important');
    d.style.setProperty(name, '-2');
    check(name + '-invalid-preserves', d.style[member], '6.78e+08');
    d.setAttribute('style', name + ': 23.4e5;');
    check(name + '-external-getter', d.style[member], '2.34e+06');
    check(name + '-external-attribute-unchanged', d.getAttribute('style'), name + ': 23.4e5;');
    d.setAttribute('title', 'unrelated');
    d.style.color = 'red';
    check(name + '-unrelated-style', d.style[member], '2.34e+06');
    check(name + '-clone', d.cloneNode(false).style[member], '2.34e+06');
    d.removeAttribute('style');
    check(name + '-external-remove', d.style[member], '');
    d.setAttribute('style', name + ': -1;');
    check(name + '-external-invalid', d.style[member], '');
  }
  var c = document.createElement('div');
  c.style.setProperty('--factor', '+.678E9');
  check('custom-untouched', c.style.getPropertyValue('--factor'), '+.678E9');
  c.style.flexGrow = '1'; c.style.flexGrow = '1px';
  check('dimension-invalid', c.style.flexGrow, '1');
  c.style.flexGrow = '1 2';
  check('two-numbers-invalid', c.style.flexGrow, '1');
  console.log('css-numbers controls: ' + passed + '/' + total);
  return passed === total;
})();
