// The external script of loop.html: it runs where its element is, and sees the elements before it.
console.log("external script", document.getElementById("late").textContent, document.getElementById("later"), document.readyState);
