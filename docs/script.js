const firebaseConfig = {
    apiKey: "AIzaSyCh-yIyBoOiPsshf2ANPeWmXZ4FF_HNgj0",
    authDomain: "queue-management-system-21507.firebaseapp.com",
    databaseURL: "https://queue-management-system-21507-default-rtdb.firebaseio.com/",
    projectId: "queue-management-system-21507",
    storageBucket: "queue-management-system-21507.firebasestorage.app",
    messagingSenderId: "32846305594",
    appId: "1:32846305594:web:bb3e35ba3bc9ac60151803"
  };
  
  // Initialize Firebase
  firebase.initializeApp(firebaseConfig);
  const database = firebase.database();
  const auth = firebase.auth();
  
  const audioPending = new Audio("assets/audio/audio1.mp3");
  const audioReady = new Audio("assets/audio/audio2.mp3");
  let previousPendingOrders = [];
  let previousReadyOrders = [];
  let currentLanguage = "en"; // Start with English
  
  // Function to toggle dark mode
  function toggleDarkMode() {
    const body = document.body;
    const darkModeIcon = document.getElementById("dark-mode-icon");
    const isDarkMode = body.classList.toggle("dark-mode");
  
    // Add fade-out class to initiate the transition
    darkModeIcon.classList.add("fade-out");
  
    // Wait for the transition to complete, then change the icon
    setTimeout(() => {
      darkModeIcon.src = isDarkMode ? "assets/images/sun.png" : "assets/images/moon.png";
      darkModeIcon.alt = isDarkMode ? "Light Mode Icon" : "Dark Mode Icon";
      darkModeIcon.classList.remove("fade-out");
    }, 300); // Duration matches the CSS transition
  
    localStorage.setItem("darkMode", isDarkMode ? "enabled" : "disabled");
  }
  
  // Initialize dark mode based on user preference
  window.onload = function () {
    const isDarkMode = localStorage.getItem("darkMode") === "enabled";
    const darkModeIcon = document.getElementById("dark-mode-icon");
  
    document.body.classList.toggle("dark-mode", isDarkMode);
    darkModeIcon.src = isDarkMode ? "assets/images/sun.png" : "assets/images/moon.png";
    darkModeIcon.alt = isDarkMode ? "Light Mode Icon" : "Dark Mode Icon";
  
    updateDateTime(); // Initialize time display
  };
  
  // Sign in anonymously and listen for order updates
  auth.signInAnonymously().then(() => {
    const ordersRef = database.ref("orders");
  
    ordersRef.on("value", (snapshot) => {
      const data = snapshot.val() || {};
      const pendingOrders = [];
      const readyOrders = [];
  
      Object.values(data).forEach((order) => {
        if (order.status === "Pending") pendingOrders.push(order.orderNumber);
        if (order.status === "Ready") readyOrders.push(order.orderNumber);
      });
  
      detectNewOrders(previousPendingOrders, pendingOrders, audioPending);
      detectNewOrders(previousReadyOrders, readyOrders, audioReady);
      previousPendingOrders = [...pendingOrders];
      previousReadyOrders = [...readyOrders];
  
      renderOrders(pendingOrders, "pending-orders");
      renderOrders(readyOrders, "ready-orders");
    });
  });
  
  // Function to detect new orders and play audio
  function detectNewOrders(prevOrders, currOrders, audio) {
    const newOrders = currOrders.filter(order => !prevOrders.includes(order));
    if (newOrders.length > 0) audio.play();
  }
  
  // Function to render orders into containers
  function renderOrders(orders, containerId) {
    const container = document.getElementById(containerId);
    container.innerHTML = orders.length
      ? orders.map(order => `<li class="order">${order}</li>`).join("")
      : `<div>No orders available.</div>`;
  }
  
  // Function to update date, time, and translations
  function updateDateTime() {
    const now = new Date();
    const options = { weekday: "long", year: "numeric", month: "long", day: "numeric", hour: "2-digit", minute: "2-digit", second: "2-digit" };
    const locale = currentLanguage === "en" ? "en-US" : "tr-TR";
  
    // Update time in the desired language
    const formattedDateTime = now.toLocaleDateString(locale, options);
    document.getElementById("date-time").innerText = formattedDateTime;
  
    // Update translations
    const translations = {
      en: { title: "Cafeteria Order System", preparingTitle: "Preparing", readyTitle: "Ready for pickup" },
      tr: { title: "Kafeterya Sipariş Sistemi", preparingTitle: "Hazırlanıyor", readyTitle: "Teslimata Hazır" }
    };
  
    const { title, preparingTitle, readyTitle } = translations[currentLanguage];
    document.getElementById("title").innerText = title;
    document.getElementById("preparing-title").innerText = preparingTitle;
    document.getElementById("ready-title").innerText = readyTitle;
  }
  
  // Function to toggle language
  function toggleLanguage() {
    currentLanguage = currentLanguage === "en" ? "tr" : "en";
    updateDateTime(); // Update time and translations immediately
  }
  
  // Update time and translations every second
  setInterval(updateDateTime, 1000);
  
  // Toggle language every 10 seconds
  setInterval(toggleLanguage, 10000);
  