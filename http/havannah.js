W13 = 12;
H12 = 21;
RADIUS = 15;

globalPlayer = null;

globalSize = null;

function drawHex(ctx, cx, cy) {
  ctx.fillStyle = 'silver';
  ctx.beginPath();
  ctx.moveTo(cx - 2 * W13, cy);
  ctx.lineTo(cx - W13, cy + H12);
  ctx.lineTo(cx + W13, cy + H12);
  ctx.lineTo(cx + 2 * W13, cy);
  ctx.lineTo(cx + W13, cy - H12);
  ctx.lineTo(cx - W13, cy - H12);
  ctx.lineTo(cx - 2 * W13, cy);
  ctx.fill();
  ctx.stroke();
  ctx.closePath();
}

function drawCircle(ctx, x, y, color) {
  ctx.fillStyle = color;
  ctx.beginPath();
  ctx.arc(x, y, RADIUS, 0, 2 * Math.PI, true);
  ctx.fill();
  ctx.stroke();
  ctx.closePath();
}

function drawSquare(ctx, player, x, y, w) {
  var luminance = [15 + 0.7 * w, 85 - 0.7 * w][player];
  ctx.fillStyle = 'hsl(120,100%,' + luminance + '%)';
  ctx.beginPath();
  ctx.rect(x - 0.01 * w * RADIUS, y - 0.01 * w * RADIUS,
           0.02 * w * RADIUS, 0.02 * w * RADIUS);
  ctx.fill();
  ctx.stroke();
  ctx.closePath();
}

function drawStone(id, player, position, result) {
  var canvas = document.getElementById(id);
  var ctx = canvas.getContext('2d');
  var l = position.charCodeAt(0) - 96;
  var n = parseInt(position.slice(1));
  var x = canvas.width / 2 + 3 * (n - l) * W13;
  var y = canvas.height - (n + l) * H12;  
  if (player == 0) {
    drawCircle(ctx, x, y, 'white');
  } else {
    drawCircle(ctx, x, y, 'black');
  }
  if (result != 0) {
    alert('result: ' + result);
    canvas.removeEventListener('click', initBoard.playMove, false);
    sendCommand.doGenerateMove = false;
  }
}

function drawBoard(id, player, legend, data) {
  var canvas = document.getElementById(id);
  var ctx = canvas.getContext('2d');
  ctx.textAlign = 'center';
  ctx.textBaseline = 'middle';
  var hexes = data.split(' ');
  var i = 0;
  var bCorner = globalSize;
  for (var l = 1; l < 2 * globalSize; ++l) {
    ++bCorner;
    var aCorner = globalSize;
    for (var n = 1; n < 2 * globalSize; ++n) {
      ++aCorner;
      if (l < aCorner && n < bCorner) {
        var x = canvas.width / 2 + 3 * (n - l) * W13;
        var y = canvas.height - (n + l) * H12;
        drawHex(ctx, x, y);
        if (hexes[i] == 'w') {
          drawCircle(ctx, x, y, 'white');
        } else if (hexes[i] == 'b') {
          drawCircle(ctx, x, y, 'black');
        } else {
          var width = parseFloat(hexes[i]);
          drawSquare(ctx, player, x, y, width);
        }
        ++i;
      }
    }
    if (legend) {
      ctx.fillStyle = 'black';
      if (l < globalSize) {
        ctx.fillText(String.fromCharCode(96 + l),
                     canvas.width / 2 - 3 * l * W13,
                     canvas.height - l * H12);
      } else {
        ctx.fillText(String.fromCharCode(96 + l),
                     canvas.width / 2 - 3 * globalSize * W13,
                     canvas.height + (globalSize - 2 * l) * H12);
      }
    }
  }
  if (legend) {
    ctx.fillStyle = 'black';
    for (var n = 1; n < 2 * globalSize; ++n) {
      if (n < globalSize) {
        ctx.fillText(n,
                     canvas.width / 2 + 3 * n * W13,
                     canvas.height - n * H12);
      } else {
        ctx.fillText(n,
                     canvas.width / 2 + 3 * globalSize * W13,
                     canvas.height + (globalSize - 2 * n) * H12);
      }
    }
  }
}

function drawStatus(list) {
  for (var i = 0; i < list.length; ++i) {
    var board = list[i];
    drawBoard(board.id, board.player, 0, board.status);
  }
  setTimeout(getStatus, 2000);
}

function sendCommand(command) {
  var newScript = document.createElement('script');
  newScript.type = 'text/javascript';
  newScript.src = '/exec?' + command;
  sendCommand.doGenerateMove = true;
  document.body.appendChild(newScript);
  initBoard.hideTooltip();
}

function badCommand(message) {
  alert(message);
  sendCommand.doGenerateMove = false;
}

function generateMove() {
  if (sendCommand.doGenerateMove) {
    sendCommand('cmd=genmove+' + 'bw'.charAt(globalPlayer));
  }  
}

function getStatus() {
  sendCommand('status=' + 'wb'.charAt(globalPlayer));
}

function initBoard(id, size, player, interaction) {
  globalSize = size;
  globalPlayer = player;
  var canvas = document.getElementById(id);
  var ctx = canvas.getContext('2d');
  canvas.width = 6 * (size + 0.5) * W13;
  canvas.height = 4 * size * H12;
  ctx.textAlign = 'center';
  ctx.textBaseline = 'middle';

  function hideTooltip() {
    if (canvas.xxTt) {
      ctx.putImageData(canvas.xxTt[0], canvas.xxTt[1], canvas.xxTt[2]);
      canvas.xxTt = null;
    }
  }

  function getHexCoordinates(e) {
    var x, y;
    if (!e) {
      e = canvas.event;
    }
    if (e.pageX || e.pageY) {
      x = e.pageX - canvas.offsetLeft;
      y = e.pageY - canvas.offsetTop;
    } else {
      x = e.clientX + document.body.scrollLeft +
          document.documentElement.scrollLeft - canvas.offsetLeft;
      y = e.clientY + document.body.scrollTop -
          document.documentElement.scrollTop - canvas.offsetTop;
    }
    var xx0 = Math.floor((x - canvas.width / 2) / W13);
    var yy = Math.floor((canvas.height - y) / H12);
    var l, n;
    var xx = Math.floor((xx0 + 1) / 3);
    if ((xx0 + 1000) % 3 != 2) {
      l = (yy - xx + 1) >> 1;
      n = (yy + xx + 1) >> 1;
    } else if ((yy - xx) % 2 == 0) {
      var dx = x - canvas.width / 2 - xx0 * W13;
      var dy = canvas.height - y - yy * H12;
      l = (yy - xx + 1) >> 1;
      n = ((yy + xx + 1) >> 1) + (dx > W13 - W13 / H12 * dy);
    } else {
      var dx = x - canvas.width / 2 - xx0 * W13;
      var dy = canvas.height - y - yy * H12;
      l = ((yy - xx + 1) >> 1) - (dx > W13 / H12 * dy);
      n = (yy + xx + 1) >> 1;
    }
    var str;
    if (l > 0 && l < 2 * size && n > 0 && n < 2 * size &&
        n - l < size && l - n < size) {
      str = String.fromCharCode(96 + l) + ' ' + n;
    } else {
      str = '';
    }
    hideTooltip();
    if (str) {
      canvas.xxTt = [ctx.getImageData(x, y + 20, 30, 18), x, y + 20];
      ctx.fillStyle = '#FFA';
      ctx.fillRect(x, y + 20, 30, 18);
      ctx.fillStyle = 'black';
      ctx.fillText(str, x + 15, y + 29);
      return String.fromCharCode(96 + l) + n;
    }
    return null;
  }

  function playMove(e) {
    coordinates = getHexCoordinates(e);
    if (!coordinates) {
      return;
    }
    hideTooltip();
    sendCommand('cmd=play+' + 'wb'.charAt(globalPlayer) + '+' + coordinates);
    setTimeout(generateMove, 1000);
  }

  initBoard.hideTooltip = hideTooltip;
  window.addEventListener('resize', hideTooltip, false);
  window.addEventListener('scroll', hideTooltip, false);
  canvas.addEventListener('mouseout', hideTooltip, false);
  canvas.addEventListener('mousemove', getHexCoordinates, false);
  if (interaction) {
    initBoard.playMove = playMove;
    canvas.addEventListener('click', playMove, false);
    sendCommand('cmd=name');
  }
}
